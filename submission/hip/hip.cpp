#include <iostream>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <cstdio>
#include <cerrno>
#include "../shared/shared_state.h"

static GlobalState* g_state = nullptr;

// Attach to the existing shared memory segment created by Arbiter.
// The HIP uses O_RDWR without O_CREAT because Arbiter owns creation.
// The HIP does not call shm_unlink; only the Arbiter should unlink the shared object.
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

    close(shm_fd); // Close the descriptor after mmap; the mapping remains valid.
    return state;
}

// Read a line from stdin safely in a threaded environment.
static bool readLine(char* buffer, size_t size) {
    if (fgets(buffer, size, stdin) == nullptr) {
        return false;
    }
    buffer[strcspn(buffer, "\n")] = '\0';
    return true;
}

// Player thread entry point.
// Only the thread whose character has is_my_turn=true should act.
// All other player threads block briefly with usleep to avoid busy spinning.
void* playerThread(void* arg) {
    GlobalState* state = g_state;
    int player_idx = *(int*)arg;
    char input[64];

    while (state->game_running) {
        sem_wait(&state->global_mutex);
        bool my_turn = state->entities[player_idx].is_my_turn;
        sem_post(&state->global_mutex);
        if (!my_turn) {
            usleep(50000);
            continue;
        }   

        sem_wait(&state->global_mutex);
        std::printf("\nHIP: Player %d's turn - current entities:\n", player_idx + 1);
        int total_entities = state->player_count + state->npc_count;
        for (int i = 0; i < total_entities; ++i) {
            std::printf("  [%d] %s HP=%d %s\n", i, state->entities[i].name,
                        state->entities[i].hp,
                        state->entities[i].is_alive ? "alive" : "dead");
        }
        sem_post(&state->global_mutex);

        std::printf("Choose action for %s:\n", state->entities[player_idx].name);
        std::printf("1. Strike\n");
        std::printf("2. Exhaust\n");
        std::printf("3. Heal\n");
        std::printf("4. Skip\n");
        std::printf("5. Use Weapon\n");
        std::printf("6. Swap In\n");
        std::printf("7. Ultimate Ability\n");
        std::printf("8. Quit\n");
        std::printf("> ");

        if (!readLine(input, sizeof(input))) {
            continue;
        }

        int choice = std::atoi(input);
        ActionType action = ActionType::NONE;
        int target_idx = -1;

        switch (choice) {
            case 1:
                action = ActionType::STRIKE;
                break;
            case 2:
                action = ActionType::EXHAUST;
                break;
            case 3:
                action = ActionType::HEAL;
                break;
            case 4:
                action = ActionType::SKIP;
                break;
            case 5:
                action = ActionType::USE_WEAPON;
                break;
            case 6:
                action = ActionType::SWAP_IN;
                break;
            case 7:
                action = ActionType::ULTIMATE;
                break;
            case 8:
                action = ActionType::QUIT;
                break;
            default:
                std::printf("Invalid choice. Try again.\n");
                continue;
        }

        if (action == ActionType::STRIKE || action == ActionType::EXHAUST) {
            target_idx = -1;
            while (target_idx < 0) {
                sem_wait(&state->global_mutex);
                std::printf("Choose target (alive enemy):\n");
                int total_entities = state->player_count + state->npc_count;
                for (int i = 0; i < total_entities; ++i) {
                    if (!state->entities[i].is_player && state->entities[i].is_alive) {
                        std::printf("  [%d] %s HP=%d\n", i, state->entities[i].name,
                                    state->entities[i].hp);
                    }
                }
                sem_post(&state->global_mutex);
                std::printf("> ");
                if (!readLine(input, sizeof(input))) {
                    continue;
                }
                int target_choice = std::atoi(input);

                sem_wait(&state->global_mutex);
                if (target_choice >= 0 && target_choice < total_entities &&
                    !state->entities[target_choice].is_player && state->entities[target_choice].is_alive) {
                    target_idx = target_choice;
                } else {
                    std::printf("Invalid target. Choose an enemy.\n");
                }
                sem_post(&state->global_mutex);
            }
        }

        sem_wait(&state->action_mutex);
        state->action_buffer.action = action;
        state->action_buffer.actor_idx = player_idx;
        state->action_buffer.target_idx = target_idx;
        state->action_buffer.weapon_slot = -1;
        sem_post(&state->action_mutex);

        sem_wait(&state->global_mutex);
        state->entities[player_idx].action_ready = true;
        sem_post(&state->global_mutex);

        if (action == ActionType::QUIT) {
            kill(getppid(), SIGTERM);
        }
    }

    return nullptr;
}

int main() {
    g_state = attachSharedMemory();
    GlobalState* state = g_state;

    while (!state->game_running) {
        usleep(500000); // Wait 500ms for Arbiter initialization
    }

    int player_count = state->player_count;
    std::vector<pthread_t> threads(player_count);
    std::vector<int> player_indices(player_count);

    for (int i = 0; i < player_count; ++i) {
        player_indices[i] = i;
        if (pthread_create(&threads[i], nullptr, playerThread, &player_indices[i]) != 0) {
            perror("pthread_create failed");
            exit(1);
        }
    }

    for (int i = 0; i < player_count; ++i) {
        pthread_join(threads[i], nullptr);
    }

    munmap(state, sizeof(GlobalState));
    std::printf("HIP: exiting\n");
    return 0;
}