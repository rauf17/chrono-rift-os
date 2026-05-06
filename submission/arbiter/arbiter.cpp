#include <iostream>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include <cerrno>
#include <cstdio>
#include "../shared/shared_state.h"

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

int main() {
    std::cout << "[arbiter] started" << std::endl;

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

    // For demonstration, append a log message
    appendLog(state, "Arbiter initialized shared memory.");

    std::cout << "[arbiter] Shared memory initialized." << std::endl;

    // In a real implementation, the arbiter would wait for other processes or handle game loop.
    // For now, just exit after initialization.

    return 0;
}