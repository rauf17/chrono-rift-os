#include <iostream>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <pthread.h>
#include <ncurses.h>
#include "../shared/shared_state.h"
#include "arbiter/inventory.h"
#include "arbiter/artifact_manager.h"
#ifndef NO_SFML
#include "../renderer/renderer.h"
#endif

static GlobalState* g_state = nullptr;
static pid_t g_hip_pid = -1;
static pid_t g_asp_pid = -1;
static std::atomic_bool shutdown_requested{false};
static std::atomic_bool ultimate_window_ended{false};
static const char kUltimateEndedMsg[] = "Ultimate window has ended.";
static bool g_tui_mode = false;

// Forward declaration for action commit logic implemented in the next prompt.
void commitAction(GlobalState* state);

// Helper function to append a message to the action log without locking.
// Use this only when the caller already holds global_mutex.
// This avoids deadlock when appendLog is invoked from inside a section
// where the global mutex is already held.
void appendLogUnsafe(GlobalState* state, const char* message) {
    std::strncpy(state->log[state->log_head % ACTION_LOG_LINES], message, ACTION_LOG_WIDTH - 1);
    state->log[state->log_head % ACTION_LOG_LINES][ACTION_LOG_WIDTH - 1] = '\0'; // Ensure null termination
    state->log_head++;
}

// Helper function to append a message to the action log with locking.
// Use this when the caller does not already hold global_mutex.
// It locks global_mutex, calls appendLogUnsafe, then unlocks global_mutex.
void appendLog(GlobalState* state, const char* message) {
    sem_wait(&state->global_mutex);
    appendLogUnsafe(state, message);
    sem_post(&state->global_mutex);
}

// Cleanup helper that terminates child processes, destroys semaphores,
// releases shared memory, unlinks the shared memory object, and exits.
void cleanupAndExit(GlobalState* state, pid_t hip_pid, pid_t asp_pid) {
    if (hip_pid > 0) {
        kill(hip_pid, SIGTERM); // Terminate HIP gracefully
        waitpid(hip_pid, nullptr, 0); // Reap HIP child process
    }
    if (asp_pid > 0) {
        kill(asp_pid, SIGTERM); // Terminate ASP gracefully
        waitpid(asp_pid, nullptr, 0); // Reap ASP child process
    }

    sem_destroy(&state->global_mutex); // Destroy process-shared semaphore
    sem_destroy(&state->action_mutex);
    munmap(state, sizeof(GlobalState)); // Unmap shared memory from address space
    if (shm_unlink("/chrono_rift_shm") == -1) { // Remove the shared memory object
        perror("shm_unlink failed");
    }
    int verify_fd = shm_open("/chrono_rift_shm", O_RDONLY, 0);
    if (verify_fd != -1) {
        close(verify_fd);
        std::fprintf(stderr, "Arbiter: shm_unlink verification failed; retrying.\n");
        if (shm_unlink("/chrono_rift_shm") == -1) {
            perror("shm_unlink retry failed");
        }
    }
    if (g_tui_mode && isendwin() == FALSE) endwin();
    std::printf("Arbiter: Shutdown complete.\n");
    exit(0);
}

// SIGTERM handler for Arbiter.
// Sets the shutdown flag and clears the game-running state.
void arbiterSigtermHandler(int signum) {
    (void)signum;
    shutdown_requested.store(true);
    if (g_state) {
        g_state->game_running = false;
    }
}

void handleSIGALRM(int signum) {
    (void)signum;
    if (g_asp_pid > 0) {
        kill(g_asp_pid, SIGCONT);
    }
    ultimate_window_ended.store(true);
}

static int artifactIndexFromName(const char* name) {
    if (!name) {
        return -1;
    }
    if (std::strcmp(name, "Solar Core") == 0) {
        return 0;
    }
    if (std::strcmp(name, "Lunar Blade") == 0) {
        return 1;
    }
    if (std::strcmp(name, "Eclipse Relic") == 0) {
        return 2;
    }
    return -1;
}

static void captureArtifactPresence(const Entity* entity, bool presence[MAX_ARTIFACTS]) {
    presence[0] = false;
    presence[1] = false;
    presence[2] = false;

    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const Weapon& slot = entity->inventory[i];
        if (!slot.occupied || slot.name[0] == '\0') {
            continue;
        }
        int idx = artifactIndexFromName(slot.name);
        if (idx >= 0) {
            presence[idx] = true;
        }
    }
}


// Launch HIP and ASP child processes using fork and exec.
// The parent sleeps briefly so children can attach to shared memory using shm_open/mmap.
void launchProcesses(pid_t& hip_pid, pid_t& asp_pid) {
    hip_pid = fork();
    if (hip_pid < 0) {
        perror("fork hip failed");
        exit(1);
    }
    if (hip_pid == 0) {
        execl("./hip/hip", "./hip/hip", nullptr);
        perror("execl hip failed");
        exit(1);
    }

    asp_pid = fork();
    if (asp_pid < 0) {
        perror("fork asp failed");
        exit(1);
    }
    if (asp_pid == 0) {
        execl("./asp/asp", "./asp/asp", nullptr);
        perror("execl asp failed");
        exit(1);
    }

    // Give child processes time to call shm_open and mmap before Arbiter starts modifying shared memory.
    sleep(1);
    std::printf("Arbiter: HIP launched with PID %d\n", hip_pid);
    std::printf("Arbiter: ASP launched with PID %d\n", asp_pid);
}

// Creates the POSIX shared memory segment for the game state.
// Uses shm_open to create "/chrono_rift_shm" with read/write permissions.
// Sets the size to sizeof(GlobalState) using ftruncate.
// Maps the segment into memory with mmap for shared access.
// Returns a pointer to the GlobalState struct.
// On failure, prints error and exits.
// Note: shm_unlink("/chrono_rift_shm") should be called on program exit to clean up.
GlobalState* createSharedMemory() {
    int shm_fd = shm_open("/chrono_rift_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        exit(1);
    }

    if (ftruncate(shm_fd, sizeof(GlobalState)) == -1) {
        perror("ftruncate failed");
        exit(1);
    }

    GlobalState* state = (GlobalState*)mmap(NULL, sizeof(GlobalState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (state == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    close(shm_fd); // File descriptor no longer needed after mmap
    return state;
}

// Initializes the semaphores in the shared memory.
// Calls sem_init with pshared=1 to enable PTHREAD_PROCESS_SHARED,
// allowing the semaphores to be used across processes since they reside in shared memory.
// Exits on failure.
void initSemaphores(GlobalState* state) {
    if (sem_init(&state->global_mutex, 1, 1) == -1) {
        perror("sem_init global_mutex failed");
        exit(1);
    }

    if (sem_init(&state->action_mutex, 1, 1) == -1) {
        perror("sem_init action_mutex failed");
        exit(1);
    }
}

// Initializes all entities and game state.
// Seeds random number generator with roll_number.
// Sets player_count and generates npc_count randomly.
// Initializes player and NPC stats using the specified formulas.
// Sets up action buffer, game flow, and artifact stubs.
// Uses srand(roll_number) for all randomness.
void initEntities(GlobalState* state, int roll_number, int player_count) {
    srand(roll_number);

    state->roll_seed = roll_number;
    state->player_count = player_count;
    int npc_count = rand() % 8 + 2;
    state->npc_count = npc_count;

    // Initialize players
    for (int i = 0; i < player_count; ++i) {
        state->entities[i].is_player = true;
        state->entities[i].is_alive = true;
        state->entities[i].hp = roll_number + (rand() % 901 + 100);
        state->entities[i].max_hp = state->entities[i].hp;
        state->entities[i].damage = (roll_number % 10) + 10;
        state->entities[i].speed = 100.0f / player_count;
        state->entities[i].stamina = 0.0f;
        state->entities[i].max_stamina = 100.0f;
        std::snprintf(state->entities[i].name, sizeof(state->entities[i].name), "Player %d", i + 1);
        state->entities[i].is_my_turn = false;
        state->entities[i].action_ready = false;
        state->entities[i].is_stunned = false;
        state->entities[i].long_term_count = 0;
        // Inventory and artifacts initialized to zero/false by default
    }

    // Initialize NPCs
    for (int i = 0; i < npc_count; ++i) {
        int idx = player_count + i;
        state->entities[idx].is_player = false;
        state->entities[idx].is_alive = true;
        int last_two_digits = roll_number % 100;
        state->entities[idx].hp = last_two_digits + (rand() % 151 + 50);
        state->entities[idx].max_hp = state->entities[idx].hp;
        int second_last_digit = (roll_number / 10) % 10;
        state->entities[idx].damage = second_last_digit + 10;
        state->entities[idx].speed = (float)(rand() % 21 + 10);
        state->entities[idx].stamina = 0.0f;
        state->entities[idx].max_stamina = 150.0f;
        std::snprintf(state->entities[idx].name, sizeof(state->entities[idx].name), "Enemy %d", i + 1);
        state->entities[idx].is_my_turn = false;
        state->entities[idx].action_ready = false;
        state->entities[idx].is_stunned = false;
        state->entities[idx].long_term_count = 0;
    }

    // Initialize action buffer
    state->action_buffer.action = ActionType::NONE;
    state->action_buffer.actor_idx = -1;
    state->action_buffer.target_idx = -1;
    state->action_buffer.weapon_slot = -1;

    // Initialize game flow
    state->game_running = true;
    state->current_turn_idx = 0;
    state->enemies_killed = 0;
    state->ultimate_active = false;
    state->log_head = 0;
    state->pending_drop_offer = false;
    state->pending_drop_name[0] = '\0';
    state->pending_drop_for_player = -1;

    // Initialize artifacts
    std::strncpy(state->artifacts[0].name, "Solar Core", sizeof(state->artifacts[0].name));
    state->artifacts[0].exists = true;
    state->artifacts[0].is_free = true;
    state->artifacts[0].held_by = -1;
    std::memset(state->artifacts[0].waiting, 0, sizeof(state->artifacts[0].waiting));

    std::strncpy(state->artifacts[1].name, "Lunar Blade", sizeof(state->artifacts[1].name));
    state->artifacts[1].exists = true;
    state->artifacts[1].is_free = true;
    state->artifacts[1].held_by = -1;
    std::memset(state->artifacts[1].waiting, 0, sizeof(state->artifacts[1].waiting));

    std::strncpy(state->artifacts[2].name, "Eclipse Relic", sizeof(state->artifacts[2].name));
    state->artifacts[2].exists = false;
    state->artifacts[2].is_free = true;
    state->artifacts[2].held_by = -1;
    std::memset(state->artifacts[2].waiting, 0, sizeof(state->artifacts[2].waiting));
}

// Main scheduling loop for stamina-driven entity turns.
// Each second, all alive, non-stunned entities accumulate stamina.
// When an entity reaches max_stamina, it is chosen to act.
// The loop unlocks the shared state before waiting for action_ready,
// allowing HIP or ASP to write the chosen action.
void schedulingLoop(GlobalState* state) {
    // Side-alternation: after all players on one side have acted once,
    // give the other side a turn. We track this with a simple flag:
    // after ANY player acts we want an NPC next, and after ANY NPC acts
    // we want a player next. Within each side we pick by highest stamina
    // so fast entities go first. If the preferred side has no one ready
    // we fall back to any ready entity to avoid deadlock.
    bool want_npc_next = false; // start by giving players the first go

    while (state->game_running && !shutdown_requested.load()) {
        struct timespec ts = {1, 0};
        nanosleep(&ts, nullptr);

        sem_wait(&state->global_mutex);

        int total_entities = state->player_count + state->npc_count;
        int actor_idx = -1;

        // Accumulate stamina for all alive, non-stunned entities.
        for (int i = 0; i < total_entities; ++i) {
            Entity& entity = state->entities[i];
            if (!entity.is_alive || entity.is_stunned) continue;
            entity.stamina += entity.speed;
            if (entity.stamina > entity.max_stamina)
                entity.stamina = entity.max_stamina;
        }

        // Collect all ready entities on the preferred side, then pick one
        // randomly so no single entity hogs turns within its side.
        bool want_player = !want_npc_next;
        int candidates[MAX_ENTITIES];
        int cand_count = 0;

        for (int i = 0; i < total_entities; ++i) {
            Entity& entity = state->entities[i];
            if (!entity.is_alive || entity.is_stunned) continue;
            if (entity.stamina < entity.max_stamina) continue;
            if (entity.is_player != want_player) continue;
            candidates[cand_count++] = i;
        }

        if (cand_count > 0) {
            // Random pick among all ready entities on the preferred side.
            actor_idx = candidates[rand() % cand_count];
        } else {
            // Fallback: preferred side not ready — pick randomly from any side.
            for (int i = 0; i < total_entities; ++i) {
                Entity& entity = state->entities[i];
                if (!entity.is_alive || entity.is_stunned) continue;
                if (entity.stamina < entity.max_stamina) continue;
                candidates[cand_count++] = i;
            }
            if (cand_count > 0)
                actor_idx = candidates[rand() % cand_count];
        }

        if (actor_idx == -1) {
            sem_post(&state->global_mutex);
            continue;
        }

        state->current_turn_idx = actor_idx;
        Entity& actor = state->entities[actor_idx];
        actor.is_my_turn = true;
        actor.action_ready = false;

        state->action_buffer.action = ActionType::NONE;
        state->action_buffer.actor_idx = actor_idx;
        state->action_buffer.target_idx = -1;
        state->action_buffer.weapon_slot = -1;

        sem_post(&state->global_mutex);
        struct timespec asp_yield = {0, 200 * 1000 * 1000};
        nanosleep(&asp_yield, nullptr);

        // Wait for the selected entity to set action_ready.
        // We poll because the shared action_ready flag is a simple cross-process signal,
        // and no blocking primitive exists for this shared-flag protocol yet.
        if (actor.is_player) {
            while (state->game_running) {
                // Poll without mutex to avoid starving renderThreadSFML and
                // the NPC threads.  The GUI thread writes action_ready under
                // the mutex so visibility is guaranteed on all platforms.
                if (state->entities[actor_idx].action_ready) {
                    break;
                }
                struct timespec wait_ts = {0, 100 * 1000 * 1000};
                nanosleep(&wait_ts, nullptr);
            }
        } else {
            int elapsed_checks = 0;
            const int max_checks = 100; // 10 seconds max
            bool ready = false;
            while (state->game_running && elapsed_checks < max_checks) {
                struct timespec wait_ts = {0, 100 * 1000 * 1000};
                nanosleep(&wait_ts, nullptr);
                // Must use the mutex here: action_ready is written by the ASP
                // child *process* (not just a thread), so a CPU memory barrier
                // is required for the write to be visible.  sem_wait/post
                // provide that barrier on Linux for MAP_SHARED memory.
                sem_wait(&state->global_mutex);
                ready = state->entities[actor_idx].action_ready;
                sem_post(&state->global_mutex);
                if (ready) {
                    break;
                }
                elapsed_checks++;
            }
            if (!ready && state->game_running) {
                std::fprintf(stderr, "[SCHED] NPC %d timed out — forcing SKIP\n", actor_idx);
                sem_wait(&state->global_mutex);
                state->action_buffer.action = ActionType::SKIP;
                state->action_buffer.actor_idx = actor_idx;
                state->entities[actor_idx].action_ready = true;
                sem_post(&state->global_mutex);
            } else {
                // Log what action the NPC actually submitted so we can verify
                sem_wait(&state->global_mutex);
                std::fprintf(stderr, "[SCHED] NPC %d ready: action=%d target=%d\n",
                             actor_idx,
                             (int)state->action_buffer.action,
                             state->action_buffer.target_idx);
                sem_post(&state->global_mutex);
            }
        }

        // Debug: log what we're about to commit
        {
            sem_wait(&state->global_mutex);
            std::fprintf(stderr, "[SCHED] committing: actor=%d action=%d target=%d\n",
                         state->action_buffer.actor_idx,
                         (int)state->action_buffer.action,
                         state->action_buffer.target_idx);
            sem_post(&state->global_mutex);
        }
        // Flip the side preference: if a player just acted, we want an NPC next.
        want_npc_next = state->entities[actor_idx].is_player;

        commitAction(state);
    }
}

// Applies the selected action to the game state, logs the result, and checks endgame conditions.
// This function assumes the action has already been selected and action_ready is true.
// It locks global_mutex for the entire commit phase to keep state updates atomic.
void checkGameConditions(GlobalState* state) {
    int alive_players = 0;
    int total_entities = state->player_count + state->npc_count;
    for (int i = 0; i < total_entities; ++i) {
        if (state->entities[i].is_player && state->entities[i].is_alive) {
            alive_players++;
        }
    }

    if (alive_players == 0) {
        appendLogUnsafe(state, "Game Over -- all players defeated");
        state->game_running = false;
        return;
    }

    if (state->enemies_killed >= state->npc_count) {
        appendLogUnsafe(state, "Victory -- all enemies defeated!");
        state->game_running = false;
    }
}

// Rendering thread for ncurses display.
// All ncurses calls happen in this single dedicated thread because ncurses is not thread-safe.
// erase() + refresh() is the correct double-buffering pattern for ncurses.
void* renderThread(void* arg) {
    GlobalState* state = (GlobalState*)arg;
    while (state->game_running) {
        usleep(200000); // 200ms refresh rate

        sem_wait(&state->global_mutex);
        int player_count = state->player_count;
        int npc_count = state->npc_count;
        int total_entities = player_count + npc_count;

        struct LocalEntity {
            char name[32];
            bool is_player;
            bool is_alive;
            bool is_my_turn;
            bool is_stunned;
            int hp;
            int max_hp;
            float stamina;
            float max_stamina;
        } entities[MAX_ENTITIES];

        for (int i = 0; i < total_entities; ++i) {
            std::memcpy(entities[i].name, state->entities[i].name, sizeof(entities[i].name));
            entities[i].is_player = state->entities[i].is_player;
            entities[i].is_alive = state->entities[i].is_alive;
            entities[i].is_my_turn = state->entities[i].is_my_turn;
            entities[i].is_stunned = state->entities[i].is_stunned;
            entities[i].hp = state->entities[i].hp;
            entities[i].max_hp = state->entities[i].max_hp;
            entities[i].stamina = state->entities[i].stamina;
            entities[i].max_stamina = state->entities[i].max_stamina;
        }

        int log_head = state->log_head;
        char log_copy[ACTION_LOG_LINES][ACTION_LOG_WIDTH];
        for (int i = 0; i < ACTION_LOG_LINES; ++i) {
            std::memcpy(log_copy[i], state->log[i], ACTION_LOG_WIDTH);
        }
        sem_post(&state->global_mutex);

        erase();
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "=== CHRONO RIFT ===");
        attroff(COLOR_PAIR(4));

        int row = 2;
        for (int i = 0; i < total_entities; ++i) {
            int hp_color = entities[i].is_player ? 1 : 2;
            if (entities[i].is_stunned) {
                hp_color = 2;
            }

            if (entities[i].is_my_turn) {
                attron(A_BOLD);
            }

            attron(COLOR_PAIR(hp_color));
            mvprintw(row, 0, "%s HP:%d/%d", entities[i].name, entities[i].hp, entities[i].max_hp);
            attroff(COLOR_PAIR(hp_color));
            if (entities[i].is_my_turn) {
                attroff(A_BOLD);
            }

            int bar_x = 35;
            int fill = 0;
            if (entities[i].max_stamina > 0.0f) {
                fill = (int)((entities[i].stamina / entities[i].max_stamina) * 20.0f + 0.5f);
            }
            if (fill < 0) fill = 0;
            if (fill > 20) fill = 20;

            attron(COLOR_PAIR(3));
            mvprintw(row, bar_x, "[");
            for (int j = 0; j < 20; ++j) {
                mvprintw(row, bar_x + 1 + j, "%c", j < fill ? '#' : '-');
            }
            mvprintw(row, bar_x + 21, "] %2d%%", entities[i].max_stamina > 0.0f ? (int)((entities[i].stamina / entities[i].max_stamina) * 100.0f) : 0);
            attroff(COLOR_PAIR(3));

            if (entities[i].is_stunned) {
                attron(COLOR_PAIR(2));
                mvprintw(row, bar_x + 27, "(STUNNED)");
                attroff(COLOR_PAIR(2));
            }
            row++;
        }

        row += 1;
        mvprintw(row++, 0, "Action Log:");
        int log_start = (log_head - 10 + ACTION_LOG_LINES) % ACTION_LOG_LINES;
        for (int i = 0; i < 10; ++i) {
            int idx = (log_start + i) % ACTION_LOG_LINES;
            if (log_copy[idx][0] == '\0') {
                continue;
            }
            attron(A_DIM);
            mvprintw(row++, 0, "%s", log_copy[idx]);
            attroff(A_DIM);
        }

        refresh();
    }

    // Clear the ncurses screen one final time when the game ends so the terminal is clean on exit.
    erase();
    refresh();

    return nullptr;
}

void* renderThreadSFML(void* arg) {
    GlobalState* state = (GlobalState*)arg;
 
    Renderer renderer("../assets", "Chrono Rift");
 
    int  prev_turn_idx    = -1;
    bool prev_turn_player = false;
    RenderSnapshot cached_snap{};   // last successfully captured snapshot
 
    int  prev_hp   [MAX_ENTITIES] = {};
    bool prev_alive[MAX_ENTITIES] = {};
    bool prev_stunned[MAX_ENTITIES] = {};
 
    {
        sem_wait(&state->global_mutex);
        int total = state->player_count + state->npc_count;
        for (int i = 0; i < total; ++i) {
            prev_hp[i]      = state->entities[i].hp;
            prev_alive[i]   = state->entities[i].is_alive;
            prev_stunned[i] = state->entities[i].is_stunned;
        }
        sem_post(&state->global_mutex);
    }
 
    while (state->game_running && renderer.isOpen()) {
        usleep(16000); // ~60fps cap; reduces global_mutex starvation

        // 1. Events
        if (!renderer.pollEvents()) {
            state->game_running = false;
            break;
        }
 
        // 2. Snapshot — try to get the mutex without blocking.  If the
        // scheduling loop currently holds it (e.g. checking action_ready),
        // we simply reuse the previous frame's snapshot.  A stale frame
        // for one render tick is invisible to the player but blocking
        // global_mutex here starves the NPC action_ready poll.
        RenderSnapshot snap;
        if (sem_trywait(&state->global_mutex) == 0) {
            // Fill snapshot while we own the mutex, then release immediately.
            snap.player_count     = state->player_count;
            snap.npc_count        = state->npc_count;
            snap.game_running     = state->game_running;
            snap.ultimate_active  = state->ultimate_active;
            snap.current_turn_idx = state->current_turn_idx;
            snap.log_head         = state->log_head;
            for (int i = 0; i < ACTION_LOG_LINES; ++i)
                std::memcpy(snap.log[i], state->log[i], ACTION_LOG_WIDTH);
            int snap_total = state->player_count + state->npc_count;
            for (int i = 0; i < snap_total && i < MAX_ENTITIES; ++i) {
                const Entity& src = state->entities[i];
                auto& dst = snap.entities[i];
                std::strncpy(dst.name, src.name, 32);
                dst.is_player         = src.is_player;
                dst.is_alive          = src.is_alive;
                dst.is_my_turn        = src.is_my_turn;
                dst.is_stunned        = src.is_stunned;
                dst.hp                = src.hp;
                dst.max_hp            = src.max_hp;
                dst.stamina           = src.stamina;
                dst.max_stamina       = src.max_stamina;
                dst.long_term_count   = src.long_term_count;
                dst.holds_solar_core  = src.holds_solar_core;
                dst.holds_lunar_blade = src.holds_lunar_blade;
                for (int s = 0; s < INVENTORY_SLOTS; ++s) {
                    std::strncpy(dst.inventory[s].name, src.inventory[s].name, 32);
                    dst.inventory[s].occupied    = src.inventory[s].occupied;
                    dst.inventory[s].slot_size   = src.inventory[s].slot_size;
                    dst.inventory[s].is_artifact = src.inventory[s].is_artifact;
                }
                for (int s = 0; s < LONG_TERM_SIZE; ++s) {
                    std::strncpy(dst.long_term[s].name, src.long_term[s].name, 32);
                    dst.long_term[s].occupied    = src.long_term[s].occupied;
                    dst.long_term[s].slot_size   = src.long_term[s].slot_size;
                    dst.long_term[s].is_artifact = src.long_term[s].is_artifact;
                }
            }
            sem_post(&state->global_mutex);
            cached_snap = snap;
        } else {
            // Mutex busy — reuse cached snapshot from previous frame.
            snap = cached_snap;
        }
        int total = snap.player_count + snap.npc_count;
 
        // 3. HP drop → hit flash; death → death anim
        for (int i = 0; i < total; ++i) {
            if (snap.entities[i].hp < prev_hp[i] && snap.entities[i].is_alive)
                renderer.triggerHitFlash(i);
            if (!snap.entities[i].is_alive && prev_alive[i])
                renderer.triggerDeath(i);
            prev_hp[i]    = snap.entities[i].hp;
            prev_alive[i] = snap.entities[i].is_alive;
        }
 
        // 4. Newly stunned → stun flash burst
        for (int i = 0; i < total; ++i) {
            if (snap.entities[i].is_stunned && !prev_stunned[i])
                renderer.triggerStunFlash(i);
            prev_stunned[i] = snap.entities[i].is_stunned;
        }
 
        // 5. Turn change → update action menu
        int  cur_turn       = snap.current_turn_idx;
        bool is_player_turn = (cur_turn >= 0 && cur_turn < total &&
                               snap.entities[cur_turn].is_player &&
                               snap.entities[cur_turn].is_my_turn);
 
        if (cur_turn != prev_turn_idx || is_player_turn != prev_turn_player) {
            prev_turn_idx    = cur_turn;
            prev_turn_player = is_player_turn;
            renderer.setPlayerTurn(is_player_turn ? cur_turn : -1, snap);
        }
 
        // 6. Weapon drop offer → show Y/N dialog in GUI
        {
            sem_wait(&state->global_mutex);
            bool  drop_pending = state->pending_drop_offer;
            int   drop_player  = state->pending_drop_for_player;
            char  drop_name[32] = {};
            if (drop_pending) {
                std::strncpy(drop_name, state->pending_drop_name,
                             sizeof(drop_name) - 1);
                drop_name[sizeof(drop_name) - 1] = '\0';
            }
            sem_post(&state->global_mutex);

            bool drop_ready = false;
            if (drop_pending && drop_player >= 0 && drop_player < total) {
                drop_ready = (drop_player == cur_turn &&
                              snap.entities[drop_player].is_player &&
                              snap.entities[drop_player].is_my_turn);
            }

            if (drop_ready) {
                // Clear the shared flag — the renderer now owns this decision
                sem_wait(&state->global_mutex);
                state->pending_drop_offer   = false;
                state->pending_drop_name[0] = '\0';
                state->pending_drop_for_player = -1;
                sem_post(&state->global_mutex);

                // Tell renderer to show the dialog for the right player
                renderer.showDropOffer(drop_player, drop_name);

                std::printf("[DROP] %s dropped for Player %d — showing dialog\n",
                            drop_name, drop_player + 1);
            }
        }
 
        // 7. Poll for completed GUI action (button click or drop dialog choice)
        GuiAction ga;
        if (renderer.pollGuiAction(ga)) {

            // Read the live actor index directly from shared memory under the
            // action_mutex to avoid using a stale cur_turn from the snapshot.
            // This prevents overwriting an NPC's pending action_buffer entry.
            int live_actor_idx = -1;
            sem_wait(&state->global_mutex);
            live_actor_idx = state->current_turn_idx;
            bool live_is_player = (live_actor_idx >= 0 &&
                                   live_actor_idx < total &&
                                   state->entities[live_actor_idx].is_player &&
                                   state->entities[live_actor_idx].is_my_turn);
            sem_post(&state->global_mutex);

            // CRITICAL GUARD: only commit a GUI action when it is genuinely
            // a player's turn right now.  Stale button clicks that arrive
            // after the turn has already flipped to an NPC must be discarded.
            if (!live_is_player) {
                // Discard the stale GUI action silently.
            } else {
                // Animations for attack-type actions
                if (ga.action == ActionType::STRIKE   ||
                    ga.action == ActionType::EXHAUST   ||
                    ga.action == ActionType::STUN)
                {
                    if (ga.target_idx >= 0) {
                        renderer.triggerAttackAnim(live_actor_idx, ga.target_idx);
                        if (ga.action == ActionType::STUN)
                            renderer.triggerStunFlash(ga.target_idx);
                    }
                }

                // Weapon float for USE_WEAPON with real slot (not pickup sentinel)
                if (ga.action == ActionType::USE_WEAPON &&
                    ga.weapon_slot >= 0 && ga.target_idx >= 0)
                {
                    const char* wname = "";
                    if (live_actor_idx >= 0 && live_actor_idx < total &&
                        ga.weapon_slot < INVENTORY_SLOTS)
                        wname = snap.entities[live_actor_idx]
                                    .inventory[ga.weapon_slot].name;
                    renderer.triggerAttackAnim(live_actor_idx, ga.target_idx);
                    renderer.triggerWeaponFloat(live_actor_idx, ga.target_idx, wname);
                    renderer.triggerHitFlash(ga.target_idx);
                }

                // Write action into shared memory using the live actor index,
                // not the potentially-stale cur_turn from the snapshot.
                sem_wait(&state->action_mutex);
                state->action_buffer.action      = ga.action;
                state->action_buffer.actor_idx   = live_actor_idx;
                state->action_buffer.target_idx  = ga.target_idx;
                state->action_buffer.weapon_slot = ga.weapon_slot;
                sem_post(&state->action_mutex);

                sem_wait(&state->global_mutex);
                state->entities[live_actor_idx].action_ready = true;
                sem_post(&state->global_mutex);

                std::printf("[GUI ACTION] %s: action=%d target=%d slot=%d\n",
                    snap.entities[live_actor_idx].name,
                    (int)ga.action, ga.target_idx, ga.weapon_slot);

                if (ga.action == ActionType::QUIT) {
                    state->game_running = false;
                    break;
                }
            }
        }
 
        // 8. Render
        renderer.render(snap);
        // During NPC turns sleep longer so the scheduling loop can acquire
        // global_mutex quickly for the action_ready poll.
        bool npc_turn = (cur_turn >= 0 && cur_turn < total && !snap.entities[cur_turn].is_player);
        usleep(npc_turn ? 200000 : 16000);
    }
 
    renderer.close();
    state->game_running = false;
    return nullptr;
}

void* deadlockMonitorThread(void* arg) {
    GlobalState* state = (GlobalState*)arg;
    while (state->game_running) {
        struct timespec ts = {1, 0};
        nanosleep(&ts, nullptr);

        int resolve_entity = -1;
        int resolve_artifact = -1;

        sem_wait(&state->global_mutex);

        bool resolved = false;
        for (int a = 0; a < MAX_ARTIFACTS && !resolved; ++a) {
            ArtifactEntry& artifact_a = state->artifacts[a];
            if (artifact_a.held_by < 0 || artifact_a.is_free) {
                continue;
            }
            int holder_x = artifact_a.held_by;

            for (int b = 0; b < MAX_ARTIFACTS; ++b) {
                if (b == a) {
                    continue;
                }
                ArtifactEntry& artifact_b = state->artifacts[b];
                if (artifact_b.held_by < 0 || artifact_b.is_free) {
                    continue;
                }
                if (!artifact_b.waiting[holder_x]) {
                    continue;
                }

                int holder_y = artifact_b.held_by;
                if (!artifact_a.waiting[holder_y]) {
                    continue;
                }

                resolve_entity = (holder_x < holder_y) ? holder_x : holder_y;
                resolve_artifact = (resolve_entity == holder_x) ? a : b;

                char msg[128];
                std::snprintf(msg, sizeof(msg), "Deadlock resolved: entity %d released %s", resolve_entity,
                              state->artifacts[resolve_artifact].name);
                appendLogUnsafe(state, msg);

                resolved = true;
                break;
            }
        }
        sem_post(&state->global_mutex);

        if (resolved && resolve_entity >= 0 && resolve_artifact >= 0) {
            releaseArtifact(state, resolve_entity, resolve_artifact);
        }
    }

    return nullptr;
}

void* stunExpiryThread(void* arg) {
    GlobalState* state = (GlobalState*)arg;
    while (state->game_running) {
        usleep(500000); // Check every 500ms
        sem_wait(&state->global_mutex);
        int total_entities = state->player_count + state->npc_count;
        for (int i = 0; i < total_entities; ++i) {
            Entity* entity = &state->entities[i];
            if (entity->is_stunned && time(nullptr) >= entity->stun_end_time) {
                entity->is_stunned = false;
                char msg[128];
                std::snprintf(msg, sizeof(msg), "%s is no longer stunned", entity->name);
                appendLogUnsafe(state, msg);
            }
        }
        sem_post(&state->global_mutex);
    }
    return nullptr;
}

void commitAction(GlobalState* state) {
    static const Weapon kDropWeapons[] = {
        {"Solar Core", 10, 95, true, false},
        {"Lunar Blade", 10, 90, true, false},
        {"Iron Halberd", 7, 55, false, false},
        {"Venom Dagger", 4, 30, false, false},
        {"Thunderstaff", 6, 50, false, false},
        {"Obsidian Axe", 5, 45, false, false},
        {"Frostbow", 6, 48, false, false},
        {"Splinter Stick", 2, 12, false, false}
    };
    static bool pending_drop_exists = false;
    static Weapon pending_drop_weapon{};
    static int pending_drop_for_player = -1;

    int acquire_count = 0;
    int acquire_entity[8];
    int acquire_artifact[8];
    int release_count = 0;
    int release_entity[8];
    int release_artifact[8];

    auto recordAcquire = [&](int entity_idx, const Weapon& weapon) {
        if (!weapon.is_artifact) {
            return;
        }
        int idx = artifactIndexFromName(weapon.name);
        if (idx < 0) {
            return;
        }
        if (acquire_count < 8) {
            acquire_entity[acquire_count] = entity_idx;
            acquire_artifact[acquire_count] = idx;
            acquire_count++;
        }
    };

    auto recordReleaseDiff = [&](int entity_idx, const bool before[MAX_ARTIFACTS], const bool after[MAX_ARTIFACTS]) {
        for (int i = 0; i < MAX_ARTIFACTS; ++i) {
            if (before[i] && !after[i]) {
                if (release_count < 8) {
                    release_entity[release_count] = entity_idx;
                    release_artifact[release_count] = i;
                    release_count++;
                }
            }
        }
    };

    sem_wait(&state->global_mutex);

    if (ultimate_window_ended.exchange(false)) {
        state->ultimate_active = false;
        appendLogUnsafe(state, kUltimateEndedMsg);
    }

    bool introduce_relic = false;

    ActionBuffer action = state->action_buffer;
    int actor_idx = action.actor_idx;
    char msg[128] = {0};

    if (actor_idx < 0 || actor_idx >= state->player_count + state->npc_count) {
        appendLogUnsafe(state, "Invalid action actor index; ignoring action.");
        state->action_buffer.action = ActionType::NONE;
        sem_post(&state->global_mutex);
        return;
    }

    Entity* actor = &state->entities[actor_idx];
    switch (action.action) {
        case ActionType::STRIKE: {
            int target_idx = action.target_idx;
            if (target_idx < 0 || target_idx >= state->player_count + state->npc_count) {
                appendLogUnsafe(state, "Strike action had invalid target index.");
                break;
            }
            Entity* target = &state->entities[target_idx];
            target->hp -= actor->damage;
            if (target->hp <= 0) {
                target->hp = 0;
                target->is_alive = false;
                if (!target->is_player) {
                    state->enemies_killed++;
                    if (rand() % 2 == 0) {        
                        Weapon dropped = kDropWeapons[rand() % 8];
                        pending_drop_weapon = dropped;
                        pending_drop_for_player = actor_idx;
                        pending_drop_exists = true;
                        state->pending_drop_offer = true;
                        std::memset(state->pending_drop_name, 0, sizeof(state->pending_drop_name));
                        std::strncpy(state->pending_drop_name, dropped.name, sizeof(state->pending_drop_name) - 1);
                        state->pending_drop_name[sizeof(state->pending_drop_name) - 1] = '\0';
                        state->pending_drop_for_player = actor_idx;
                        std::snprintf(msg, sizeof(msg), "Weapon dropped: %s (for actor %d)", pending_drop_weapon.name, pending_drop_for_player);
                        appendLogUnsafe(state, msg);
                    }
                }
            }
            std::snprintf(msg, sizeof(msg), "%s used Strike on %s for %d damage", actor->name, target->name, actor->damage);
            appendLogUnsafe(state, msg);
            actor->stamina = 0.0f;
            break;
        }
        case ActionType::EXHAUST: {
            int target_idx = action.target_idx;
            if (target_idx < 0 || target_idx >= state->player_count + state->npc_count) {
                appendLogUnsafe(state, "Exhaust action had invalid target index.");
                break;
            }
            Entity* target = &state->entities[target_idx];
            target->stamina -= actor->damage;
            if (target->stamina < 0.0f) {
                target->stamina = 0.0f;
            }
            std::snprintf(msg, sizeof(msg), "%s used Exhaust on %s", actor->name, target->name);
            appendLogUnsafe(state, msg);
            actor->stamina = 0.0f;
            break;
        }
        case ActionType::HEAL: {
            int heal_amount = (int)(actor->max_hp * 0.10f);
            actor->hp += heal_amount;
            if (actor->hp > actor->max_hp) {
                actor->hp = actor->max_hp;
            }
            std::snprintf(msg, sizeof(msg), "%s healed for %d HP", actor->name, heal_amount);
            appendLogUnsafe(state, msg);
            actor->stamina = 0.0f;
            break;
        }
        case ActionType::SKIP: {
            if (pending_drop_exists && actor_idx == pending_drop_for_player) {
                int npc_indices[MAX_NPCS];
                int npc_count = 0;
                int start = state->player_count;
                int end = state->player_count + state->npc_count;
                for (int i = start; i < end; ++i) {
                    if (state->entities[i].is_alive) {
                        npc_indices[npc_count++] = i;
                    }
                }

                if (npc_count > 0) {
                    static unsigned int drop_seed = 0;
                    if (drop_seed == 0) {
                        drop_seed = (unsigned int)time(nullptr) ^ (unsigned int)getpid();
                        if (drop_seed == 0) {
                            drop_seed = 1;
                        }
                    }
                    int npc_idx = npc_indices[rand_r(&drop_seed) % npc_count];
                    acquireWeapon(&state->entities[npc_idx], pending_drop_weapon);
                    std::snprintf(msg, sizeof(msg), "%s picked up %s", state->entities[npc_idx].name, pending_drop_weapon.name);
                    appendLogUnsafe(state, msg);
                } else {
                    appendLogUnsafe(state, "No NPC available to claim the dropped weapon.");
                }
                pending_drop_exists = false;
                pending_drop_for_player = -1;
            }
            actor->stamina = actor->max_stamina * 0.50f;
            std::snprintf(msg, sizeof(msg), "%s skipped their turn", actor->name);
            appendLogUnsafe(state, msg);
            break;
        }
        case ActionType::USE_WEAPON:
            if (action.weapon_slot == -2) {
                if (!pending_drop_exists) {
                    appendLogUnsafe(state, "No pending weapon drop to pick up.");
                    break;
                }
                bool before[MAX_ARTIFACTS];
                bool after[MAX_ARTIFACTS];
                captureArtifactPresence(actor, before);
                acquireWeapon(actor, pending_drop_weapon);
                captureArtifactPresence(actor, after);
                recordReleaseDiff(actor_idx, before, after);
                recordAcquire(actor_idx, pending_drop_weapon);
                std::snprintf(msg, sizeof(msg), "%s picked up %s", actor->name, pending_drop_weapon.name);
                appendLogUnsafe(state, msg);
                pending_drop_exists = false;
                pending_drop_for_player = -1;
                actor->stamina = 0.0f;
                break;
            }

            if (action.weapon_slot < 0 || action.weapon_slot >= INVENTORY_SLOTS) {
                appendLogUnsafe(state, "Use Weapon had invalid inventory index.");
                break;
            }

            if (!actor->inventory[action.weapon_slot].occupied || actor->inventory[action.weapon_slot].name[0] == '\0') {
                appendLogUnsafe(state, "Use Weapon failed: empty inventory slot.");
                break;
            }

            if (action.target_idx < 0 || action.target_idx >= state->player_count + state->npc_count) {
                appendLogUnsafe(state, "Use Weapon had invalid target index.");
                break;
            }

            {
                Entity* target = &state->entities[action.target_idx];
                Weapon weapon = actor->inventory[action.weapon_slot];
                target->hp -= weapon.damage;
                if (target->hp <= 0) {
                    target->hp = 0;
                    target->is_alive = false;
                    if (!target->is_player) {
                        state->enemies_killed++;
                    }
                }
                std::snprintf(msg, sizeof(msg), "%s used %s on %s for %d damage", actor->name, weapon.name, target->name, weapon.damage);
                appendLogUnsafe(state, msg);
            }
            actor->stamina = 0.0f;
            break;
        case ActionType::SWAP_IN:
            if (actor->long_term_count <= 0) {
                appendLogUnsafe(state, "Swap In failed: long-term storage is empty.");
                break;
            }

            if (action.weapon_slot < 0 || action.weapon_slot >= actor->long_term_count) {
                appendLogUnsafe(state, "Swap In failed: invalid long-term index.");
                break;
            }

            {
                Weapon weapon = actor->long_term[action.weapon_slot];
                for (int i = action.weapon_slot; i < actor->long_term_count - 1; ++i) {
                    actor->long_term[i] = actor->long_term[i + 1];
                }
                actor->long_term_count--;
                bool before[MAX_ARTIFACTS];
                bool after[MAX_ARTIFACTS];
                captureArtifactPresence(actor, before);
                acquireWeapon(actor, weapon);
                captureArtifactPresence(actor, after);
                recordReleaseDiff(actor_idx, before, after);
                recordAcquire(actor_idx, weapon);
                std::snprintf(msg, sizeof(msg), "%s swapped in %s", actor->name, weapon.name);
                appendLogUnsafe(state, msg);
                actor->stamina = 0.0f;
            }
            break;
        case ActionType::ULTIMATE:
            if (state->ultimate_active) {
                appendLogUnsafe(state, "Ultimate is already active.");
                break;
            }
            if (!actor->holds_solar_core || !actor->holds_lunar_blade) {
                appendLogUnsafe(state, "Ultimate failed: missing required artifacts.");
                break;
            }
            std::snprintf(msg, sizeof(msg), "%s activated Ultimate Ability", actor->name);
            appendLogUnsafe(state, msg);
            state->ultimate_active = true;
            if (g_asp_pid > 0) {
                kill(g_asp_pid, SIGSTOP);
            }
            alarm(10);
            actor->stamina = 0.0f;
            break;
        case ActionType::STUN:
        {
            int target_idx = action.target_idx;
            if (target_idx < state->player_count || target_idx >= state->player_count + state->npc_count) {
                appendLogUnsafe(state, "Stun action had invalid target index.");
                break;
            }
            Entity* target = &state->entities[target_idx];
            if (!target->is_alive) {
                appendLogUnsafe(state, "Stun action had a dead target.");
                break;
            }
            target->is_stunned = true;
            target->stun_end_time = time(nullptr) + 3;
            std::snprintf(msg, sizeof(msg), "%s stunned %s", actor->name, target->name);
            appendLogUnsafe(state, msg);
            if (g_asp_pid > 0) {
                kill(g_asp_pid, SIGUSR1);
            }
            actor->stamina = 0.0f;
            break;
        }
        case ActionType::QUIT:
            appendLogUnsafe(state, "Player chose to quit. Shutting down.");
            state->game_running = false;
            break;
        case ActionType::NONE:
        default:
            appendLogUnsafe(state, "No action to commit.");
            break;
    }

    actor->is_my_turn = false;
    state->action_buffer.action = ActionType::NONE;

    if (state->enemies_killed == 5 && !state->artifacts[2].exists) {
        introduce_relic = true;
    }

    checkGameConditions(state);
    sem_post(&state->global_mutex);

    for (int i = 0; i < release_count; ++i) {
        releaseArtifact(state, release_entity[i], release_artifact[i]);
    }

    for (int i = 0; i < acquire_count; ++i) {
        tryAcquireArtifact(state, acquire_entity[i], acquire_artifact[i]);
    }

    if (introduce_relic) {
        introduceEclipseRelic(state);
    }
}

int main() {
    std::cout << "Arbiter: Starting Chrono Rift..." << std::endl;

    GlobalState* state = createSharedMemory();
    state->arbiter_pid = getpid();
    initSemaphores(state);

    int roll_number;
    int player_count;
    std::cout << "Enter your Roll Number (as integer): ";
    std::cin >> roll_number;
    std::cout << "Enter number of player characters (1-4): ";
    std::cin >> player_count;
    if (player_count < 1 || player_count > 4) {
        std::cerr << "Invalid player count. Must be between 1 and 4." << std::endl;
        cleanupAndExit(state, -1, -1);
    }

    std::cout << "Choose display mode: 1) TUI (ncurses)  2) GUI (SFML): ";
    int display_mode;
    std::cin >> display_mode;

    initEntities(state, roll_number, player_count);
    appendLog(state, "Game initialized. Waiting for processes...");

    g_state = state;

    struct sigaction sa{};
    sa.sa_handler = arbiterSigtermHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        perror("sigaction failed");
        cleanupAndExit(state, -1, -1);
    }
    // Partner B may add SIGUSR1 and SIGALRM handlers here later.
    struct sigaction sa_alrm{};
    sa_alrm.sa_handler = handleSIGALRM;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = 0;
    if (sigaction(SIGALRM, &sa_alrm, nullptr) == -1) {
        perror("sigaction SIGALRM failed");
        cleanupAndExit(state, -1, -1);
    }

    pthread_t render_tid;
    if (display_mode == 2) {
        // SFML path — do NOT init ncurses
        g_tui_mode = false;
        pthread_create(&render_tid, nullptr, renderThreadSFML, state);
    } else {
        // TUI path — init ncurses exactly as it was before
        g_tui_mode = true;
        initscr(); cbreak(); noecho(); curs_set(0); start_color();
        init_pair(1, COLOR_GREEN, COLOR_BLACK);
        init_pair(2, COLOR_RED, COLOR_BLACK);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(4, COLOR_CYAN, COLOR_BLACK);
        pthread_create(&render_tid, nullptr, renderThread, state);
    }

    pthread_t deadlock_tid;
    if (pthread_create(&deadlock_tid, nullptr, deadlockMonitorThread, state) != 0) {
        perror("pthread_create deadlock monitor failed");
        cleanupAndExit(state, -1, -1);
    }

    pthread_t stun_expiry_tid;
    if (pthread_create(&stun_expiry_tid, nullptr, stunExpiryThread, state) != 0) {
        perror("pthread_create stun expiry failed");
        cleanupAndExit(state, -1, -1);
    }

    pid_t hip_pid = -1;
    pid_t asp_pid = -1;
 
    if (display_mode == 2) {
        asp_pid = fork();
        if (asp_pid < 0) { perror("fork asp failed"); exit(1); }
        if (asp_pid == 0) {
            execl("./asp/asp", "./asp/asp", nullptr);
            perror("execl asp failed");
            exit(1);
        }
        sleep(1);
        std::printf("Arbiter: ASP launched PID=%d (GUI mode, HIP skipped)\n",
                    asp_pid);
    } else {
        launchProcesses(hip_pid, asp_pid);
    }
    g_hip_pid = hip_pid;
    g_asp_pid = asp_pid;

    sleep(1);
    appendLog(state, "All processes online. Game starting.");

    // Enter the stamina-based scheduling loop.
    schedulingLoop(state);

    state->game_running = false;
    pthread_join(render_tid, nullptr);
    pthread_join(deadlock_tid, nullptr);
    pthread_join(stun_expiry_tid, nullptr);
    cleanupAndExit(state, hip_pid, asp_pid);
    return 0;
}