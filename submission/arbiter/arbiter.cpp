#include <iostream>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <ncurses.h>
#include <cerrno>
#include <cstdio>
#include "../shared/shared_state.h"

static GlobalState* g_state = nullptr;
static pid_t g_hip_pid = -1;
static pid_t g_asp_pid = -1;

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
    shm_unlink("/chrono_rift_shm"); // Remove the shared memory object
    if (isendwin() == FALSE) endwin();
    std::printf("Arbiter: Shutdown complete.\n");
    exit(0);
}

// SIGTERM handler for Arbiter.
// Sets the game_running flag false and performs process cleanup.
void arbiterSigtermHandler(int signum) {
    (void)signum;
    if (g_state) {
        g_state->game_running = false;
    }
    cleanupAndExit(g_state, g_hip_pid, g_asp_pid);
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
    while (state->game_running) {
        struct timespec ts = {1, 0};
        // nanosleep is preferred over sleep(1) because sleep() can be interrupted by signals.
        // Using nanosleep avoids interaction issues with SIGALRM later.
        nanosleep(&ts, nullptr);

        sem_wait(&state->global_mutex);

        int total_entities = state->player_count + state->npc_count;
        int actor_idx = -1;

        for (int i = 0; i < total_entities; ++i) {
            Entity& entity = state->entities[i];
            if (!entity.is_alive || entity.is_stunned) {
                continue;
            }

            entity.stamina += entity.speed;
            if (entity.stamina > entity.max_stamina) {
                entity.stamina = entity.max_stamina;
            }

            if (actor_idx == -1 && entity.stamina >= entity.max_stamina) {
                actor_idx = i;
            }
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

        // Wait for the selected entity to set action_ready.
        // We poll because the shared action_ready flag is a simple cross-process signal,
        // and no blocking primitive exists for this shared-flag protocol yet.
        if (actor.is_player) {
            while (state->game_running) {
                sem_wait(&state->global_mutex);
                bool ready = state->entities[actor_idx].action_ready;
                sem_post(&state->global_mutex);
                if (ready) {
                    break;
                }
                struct timespec wait_ts = {0, 100 * 1000 * 1000};
                nanosleep(&wait_ts, nullptr);
            }
        } else {
            int elapsed_checks = 0;
            const int max_checks = 30; // 3 seconds at 100ms per poll
            bool ready = false;
            while (state->game_running && elapsed_checks < max_checks) {
                sem_wait(&state->global_mutex);
                ready = state->entities[actor_idx].action_ready;
                sem_post(&state->global_mutex);
                if (ready) {
                    break;
                }
                struct timespec wait_ts = {0, 100 * 1000 * 1000};
                nanosleep(&wait_ts, nullptr);
                elapsed_checks++;
            }
            if (!ready && state->game_running) {
                sem_wait(&state->global_mutex);
                state->action_buffer.action = ActionType::SKIP;
                state->action_buffer.actor_idx = actor_idx;
                state->entities[actor_idx].action_ready = true;
                sem_post(&state->global_mutex);
            }
        }

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

    if (state->enemies_killed >= 10) {
        appendLogUnsafe(state, "Victory -- 10 enemies defeated!");
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

void commitAction(GlobalState* state) {
    sem_wait(&state->global_mutex);

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
            actor->stamina = actor->max_stamina * 0.50f;
            std::snprintf(msg, sizeof(msg), "%s skipped their turn", actor->name);
            appendLogUnsafe(state, msg);
            break;
        }
        case ActionType::USE_WEAPON:
            std::snprintf(msg, sizeof(msg), "%s used Use Weapon -- Partner B to implement", actor->name);
            appendLogUnsafe(state, msg);
            actor->stamina = 0.0f;
            // TODO: Partner B implements this
            break;
        case ActionType::SWAP_IN:
            std::snprintf(msg, sizeof(msg), "%s used Swap In -- Partner B to implement", actor->name);
            appendLogUnsafe(state, msg);
            actor->stamina = 0.0f;
            // TODO: Partner B implements this
            break;
        case ActionType::ULTIMATE:
            std::snprintf(msg, sizeof(msg), "%s used Ultimate -- Partner B to implement", actor->name);
            appendLogUnsafe(state, msg);
            actor->stamina = 0.0f;
            // TODO: Partner B implements this
            break;
        case ActionType::STUN:
            std::snprintf(msg, sizeof(msg), "%s used Stun -- Partner B to implement", actor->name);
            appendLogUnsafe(state, msg);
            actor->stamina = 0.0f;
            // TODO: Partner B implements this
            break;
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

    checkGameConditions(state);
    sem_post(&state->global_mutex);
}

int main() {

    int roll_number, player_count;
    std::cout << "Enter roll number: ";
    std::cin >> roll_number;
    std::cout << "Enter number of players (1-4): ";
    std::cin >> player_count;
    if (player_count < 1 || player_count > 4) {
        std::cerr << "Invalid player count. Must be between 1 and 4." << std::endl;
        return 1;
    }

    GlobalState* state = createSharedMemory();
    initSemaphores(state);
    initEntities(state, roll_number, player_count);

    g_state = state;

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_RED, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);

    pthread_t render_thread;
    if (pthread_create(&render_thread, nullptr, renderThread, state) != 0) {
        perror("pthread_create render thread failed");
        cleanupAndExit(state, g_hip_pid, g_asp_pid);
    }

    struct sigaction sa{};
    sa.sa_handler = arbiterSigtermHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        perror("sigaction failed");
        cleanupAndExit(state, g_hip_pid, g_asp_pid);
    }

    pid_t hip_pid = -1;
    pid_t asp_pid = -1;
    launchProcesses(hip_pid, asp_pid);
    g_hip_pid = hip_pid;
    g_asp_pid = asp_pid;

    // For demonstration, append a log message
    appendLog(state, "Arbiter initialized shared memory.");

    std::cout << "[arbiter] Shared memory initialized." << std::endl;

    // Enter the stamina-based scheduling loop.
    schedulingLoop(state);

    pthread_join(render_thread, nullptr);
    cleanupAndExit(state, hip_pid, asp_pid);
    return 0;
}