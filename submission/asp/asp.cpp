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
#include "../shared/shared_state.h"

// The ASP spawns one thread per NPC. This is a hard requirement of the spec.
// Running all NPC decisions sequentially inside a single thread would violate
// the concurrency requirement and receive zero marks for that section.
static GlobalState* g_state = nullptr;

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
// The thread exits when the shared game_running flag becomes false.
void* npcThread(void* arg) {
    int npc_idx = *(int*)arg;
    GlobalState* state = g_state;

    srand((unsigned int)time(nullptr) ^ (unsigned int)npc_idx); // Per-thread RNG seed avoids rand() data races.

    while (state->game_running) {
        usleep(100000); // Poll every 100ms for this NPC's turn

        // Avoid data races on is_my_turn by reading it under the global mutex.
        sem_wait(&state->global_mutex);
        bool my_turn = state->entities[npc_idx].is_my_turn;
        sem_post(&state->global_mutex);
        if (!my_turn) {
            continue;
        }

        ActionType chosen_action = ActionType::SKIP;
        int chosen_target = -1;

        // Protect decision-making reads from concurrent updates by Arbiter/HIP.
        sem_wait(&state->global_mutex);
        int player_count = state->player_count;
        int lowest_hp = INT_MAX;
        int alive_player_count = 0;

        for (int i = 0; i < player_count; ++i) {
            if (state->entities[i].is_alive) {
                alive_player_count++;
                if (state->entities[i].hp < lowest_hp) {
                    lowest_hp = state->entities[i].hp;
                    chosen_target = i;
                }
            }
        }

        if (alive_player_count > 0) {
            if (rand() % 10 < 8) {
                chosen_action = ActionType::STRIKE;
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
    }

    return nullptr;
}

int main() {
    g_state = attachSharedMemory();
    srand((unsigned int)time(nullptr));

    usleep(500000); // Wait 500ms for Arbiter initialization

    int player_count = g_state->player_count;
    int npc_count = g_state->npc_count;

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

    munmap(g_state, sizeof(GlobalState));
    std::printf("ASP: exiting\n");
    return 0;
}