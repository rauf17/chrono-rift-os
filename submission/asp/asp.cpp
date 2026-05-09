#include <iostream>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <climits>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <cstdio>
#include <cerrno>
#include <ctime>
#include <csignal>
#include "../shared/shared_state.h"

// The ASP spawns one thread per NPC. This is a hard requirement of the spec.
// Running all NPC decisions sequentially inside a single thread would violate
// the concurrency requirement and receive zero marks for that section.
static GlobalState* g_state = nullptr;
static volatile sig_atomic_t stun_signal_received = 0;

void handleSIGUSR1(int signum) {
    (void)signum;
    stun_signal_received = 1;
}

void* stunWatcherThread(void* arg) {
    (void)arg;
    while (g_state && g_state->game_running) {
        if (!stun_signal_received) {
            struct timespec poll_ts = {0, 50 * 1000 * 1000};
            nanosleep(&poll_ts, nullptr);
            continue;
        }

        // Reset the signal flag and acknowledge the stun signal.
        // The Arbiter is the authority on stun state: it sets is_stunned = true
        // directly on the target NPC in shared memory before sending SIGUSR1, and
        // the scheduling loop already skips stunned entities. The ASP only needs
        // to acknowledge the signal; it does not modify any entity's stun state.
        stun_signal_received = 0;
        std::fprintf(stderr, "ASP: Acknowledged stun signal from Arbiter\n");
    }

    return nullptr;
}

// Attach to the existing shared memory segment created by Arbiter.
// Both HIP and ASP use the same shared memory name so Arbiter can talk to both
// without any pipes.
GlobalState* attachSharedMemory() {
    int shm_fd = shm_open("/chrono_rift_shm", O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        exit(1);
    }

    GlobalState* state = (GlobalState*)mmap(NULL, sizeof(GlobalState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    close(shm_fd); // Descriptor no longer needed after mmap
    return state;
}

// Each NPC thread waits for its own turn and writes a decision into shared memory.
// The thread exits when the shared game_running flag becomes false OR its entity dies.
void* npcThread(void* arg) {
    int npc_idx = *(int*)arg;
    GlobalState* state = g_state;

    unsigned int thread_seed = (unsigned int)time(nullptr) ^ (unsigned int)npc_idx;

    while (state->game_running) {
        usleep(100000); // Poll every 100ms for this NPC's turn

        // Avoid data races on entity state by reading it under the global mutex.
        sem_wait(&state->global_mutex);
        bool my_turn = state->entities[npc_idx].is_my_turn;
        bool is_stunned = state->entities[npc_idx].is_stunned;
        bool is_alive = state->entities[npc_idx].is_alive;
        sem_post(&state->global_mutex);
        if (!is_alive) {
            return nullptr;
        }
        if (is_stunned) {
            usleep(100000);
            continue;
        }
        if (!my_turn) {
            usleep(100000);
            continue;
        }

        ActionType chosen_action = ActionType::SKIP;
        int chosen_target = -1;

        // Protect decision-making reads from concurrent updates by Arbiter/HIP.
        sem_wait(&state->global_mutex);
        int player_count = state->player_count;
        int alive_player_indices[MAX_PLAYERS];
        int alive_player_count = 0;

        for (int i = 0; i < player_count; ++i) {
            if (state->entities[i].is_alive) {
                alive_player_indices[alive_player_count++] = i;
            }
        }

        if (alive_player_count > 0) {
            if (rand_r(&thread_seed) % 10 < 8) {
                chosen_action = ActionType::STRIKE;
                // Pick a random alive player as target
                chosen_target = alive_player_indices[rand_r(&thread_seed) % alive_player_count];
            } else {
                chosen_action = ActionType::SKIP;
                chosen_target = -1;
            }
        } else {
            chosen_action = ActionType::SKIP;
            chosen_target = -1;
        }
        sem_post(&state->global_mutex);

        sem_wait(&state->action_mutex);
        state->action_buffer.action = chosen_action;
        state->action_buffer.actor_idx = npc_idx;
        state->action_buffer.target_idx = chosen_target;
        state->action_buffer.weapon_slot = -1;
        sem_post(&state->action_mutex);

        sem_wait(&state->global_mutex);
        state->entities[npc_idx].action_ready = true;
        sem_post(&state->global_mutex);

        usleep(500000);
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// waveSpawnWatcherThread
// Polls the wave_spawn_pending flag.  When it fires, spawns fresh npcThread
// threads for each entity index listed in wave_spawn_indices.
// This keeps the ASP main loop clean and allows unlimited wave spawning.
// ---------------------------------------------------------------------------

// We keep a registry of all dynamically-spawned thread handles + their heap-
// allocated index so we can free them on exit.
struct NpcThreadEntry {
    pthread_t tid;
    int*      idx_ptr; // heap-allocated int owned by this entry
};

static std::vector<NpcThreadEntry> g_dynamic_threads;
static pthread_mutex_t             g_dynamic_mutex = PTHREAD_MUTEX_INITIALIZER;

void* waveSpawnWatcherThread(void* arg) {
    (void)arg;
    GlobalState* state = g_state;

    while (state->game_running) {
        struct timespec poll_ts = {0, 100 * 1000 * 1000}; // 100ms
        nanosleep(&poll_ts, nullptr);

        // Check flag without holding the mutex for long.
        sem_wait(&state->global_mutex);
        bool pending = state->wave_spawn_pending;
        int  count   = state->wave_spawn_count;
        int  indices[MAX_NPCS];
        if (pending && count > 0 && count <= MAX_NPCS) {
            for (int i = 0; i < count; ++i)
                indices[i] = state->wave_spawn_indices[i];
        }
        if (pending) {
            // Acknowledge — clear the flag so Arbiter knows we saw it.
            state->wave_spawn_pending = false;
            state->wave_spawn_count   = 0;
        }
        sem_post(&state->global_mutex);

        if (!pending || count <= 0) continue;

        std::fprintf(stderr, "ASP: Spawning %d new NPC threads for wave.\n", count);

        for (int i = 0; i < count; ++i) {
            int entity_idx = indices[i];
            if (entity_idx < 0 || entity_idx >= MAX_ENTITIES) continue;

            // Allocate a persistent int for the thread argument.
            int* idx_ptr = new int(entity_idx);

            pthread_t tid;
            if (pthread_create(&tid, nullptr, npcThread, idx_ptr) != 0) {
                perror("ASP: pthread_create wave NPC failed");
                delete idx_ptr;
                continue;
            }

            NpcThreadEntry entry{tid, idx_ptr};
            pthread_mutex_lock(&g_dynamic_mutex);
            g_dynamic_threads.push_back(entry);
            pthread_mutex_unlock(&g_dynamic_mutex);

            std::fprintf(stderr, "ASP: Spawned thread for entity %d\n", entity_idx);
        }
    }

    return nullptr;
}

int main() {
    g_state = attachSharedMemory();
    srand((unsigned int)time(nullptr));

    struct sigaction sa{};
    sa.sa_handler = handleSIGUSR1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, nullptr) == -1) {
        perror("sigaction SIGUSR1 failed");
        exit(1);
    }

    usleep(500000); // Wait 500ms for Arbiter initialization

    int player_count = g_state->player_count;
    int npc_count = g_state->npc_count;

    pthread_t stun_tid;
    if (pthread_create(&stun_tid, nullptr, stunWatcherThread, nullptr) != 0) {
        perror("pthread_create stun watcher failed");
        exit(1);
    }

    // Spawn the wave-spawn watcher thread.
    pthread_t wave_tid;
    if (pthread_create(&wave_tid, nullptr, waveSpawnWatcherThread, nullptr) != 0) {
        perror("pthread_create wave watcher failed");
        exit(1);
    }

    // Spawn one NPC thread per initial enemy using stable heap indices so
    // the pointer remains valid for the lifetime of the thread.
    std::vector<pthread_t> threads(npc_count);
    std::vector<int> npc_indices(npc_count);

    for (int i = 0; i < npc_count; ++i) {
        npc_indices[i] = player_count + i;
        if (pthread_create(&threads[i], nullptr, npcThread, &npc_indices[i]) != 0) {
            perror("pthread_create failed");
            exit(1);
        }
    }

    while (g_state->game_running) {
        usleep(100000);
    }

    for (int i = 0; i < npc_count; ++i) {
        pthread_join(threads[i], nullptr);
    }

    // Join any dynamically-spawned threads and free their index memory.
    pthread_join(wave_tid, nullptr);
    {
        pthread_mutex_lock(&g_dynamic_mutex);
        for (auto& entry : g_dynamic_threads) {
            pthread_join(entry.tid, nullptr);
            delete entry.idx_ptr;
        }
        g_dynamic_threads.clear();
        pthread_mutex_unlock(&g_dynamic_mutex);
    }

    pthread_join(stun_tid, nullptr);

    munmap(g_state, sizeof(GlobalState));
    std::printf("ASP: exiting\n");
    return 0;
}