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
static pthread_mutex_t input_mutex = PTHREAD_MUTEX_INITIALIZER;

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
        bool has_drop = state->pending_drop_offer && state->pending_drop_for_player == player_idx;
        bool drop_ready = state->entities[player_idx].is_my_turn;
        char drop_name[32] = {0};
        if (has_drop && drop_ready) {
            std::strncpy(drop_name, state->pending_drop_name, sizeof(drop_name) - 1);
            drop_name[sizeof(drop_name) - 1] = '\0';
            state->pending_drop_offer = false;
            state->pending_drop_name[0] = '\0';
            state->pending_drop_for_player = -1;
        }
        sem_post(&state->global_mutex);

        if (has_drop) {
            int pickup_choice = 0;
            while (pickup_choice != 1 && pickup_choice != 2) {
                pthread_mutex_lock(&input_mutex);
                std::printf("Weapon dropped: %s. Pick it up? (1 = Yes, 2 = No)\n> ", drop_name);
                char drop_input[64];
                bool got = readLine(drop_input, sizeof(drop_input));
                pthread_mutex_unlock(&input_mutex);
                if (!got) {
                    continue;
                }
                pickup_choice = std::atoi(drop_input);
            }
            sem_wait(&state->action_mutex);
            state->action_buffer.actor_idx = player_idx;
            state->action_buffer.target_idx = -1;
            if (pickup_choice == 1) {
                state->action_buffer.action = ActionType::USE_WEAPON;
                state->action_buffer.weapon_slot = -2;
            } else {
                state->action_buffer.action = ActionType::SKIP;
                state->action_buffer.weapon_slot = -1;
            }
            sem_post(&state->action_mutex);
            sem_wait(&state->global_mutex);
            state->entities[player_idx].action_ready = true;
            sem_post(&state->global_mutex);
            usleep(200000);
        }

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

        pthread_mutex_lock(&input_mutex);
        std::printf("Choose action for %s:\n", state->entities[player_idx].name);
        std::printf("1. Strike\n");
        std::printf("2. Exhaust\n");
        std::printf("3. Heal\n");
        std::printf("4. Skip\n");
        std::printf("5. Use Weapon\n");
        std::printf("6. Swap In\n");
        std::printf("7. Ultimate Ability\n");
        std::printf("8. Quit\n");
        std::printf("9. Stun\n");
        std::printf("> ");
        bool got_choice = readLine(input, sizeof(input));
        pthread_mutex_unlock(&input_mutex);
        if (!got_choice) {
            continue;
        }

        int choice = std::atoi(input);
        ActionType action = ActionType::NONE;
        int target_idx = -1;
        int weapon_slot = -1;

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
            case 9:
                action = ActionType::STUN;
                break;
            default:
                std::printf("Invalid choice. Try again.\n");
                continue;
        }

        if (action == ActionType::STRIKE || action == ActionType::EXHAUST || action == ActionType::STUN) {
            target_idx = -1;
            while (target_idx < 0) {
                pthread_mutex_lock(&input_mutex);
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
                bool got_target = readLine(input, sizeof(input));
                pthread_mutex_unlock(&input_mutex);
                if (!got_target) {
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

        if (action == ActionType::USE_WEAPON) {
            while (weapon_slot < 0) {
                bool has_weapon = false;
                pthread_mutex_lock(&input_mutex);
                sem_wait(&state->global_mutex);
                std::printf("Available weapons:\n");
                for (int i = 0; i < INVENTORY_SLOTS; ++i) {
                    const Weapon& slot = state->entities[player_idx].inventory[i];
                    if (slot.occupied && slot.name[0] != '\0') {
                        std::printf("  [%d] %s (DMG=%d, SIZE=%d)\n", i, slot.name, slot.damage, slot.slot_size);
                        has_weapon = true;
                    }
                }
                sem_post(&state->global_mutex);

                if (!has_weapon) {
                    std::printf("No weapons available in inventory.\n");
                    pthread_mutex_unlock(&input_mutex);
                    action = ActionType::SKIP;
                    break;
                }

                std::printf("Choose weapon slot index:\n> ");
                bool got_slot = readLine(input, sizeof(input));
                pthread_mutex_unlock(&input_mutex);
                if (!got_slot) {
                    continue;
                }
                int slot_choice = std::atoi(input);

                sem_wait(&state->global_mutex);
                bool valid = slot_choice >= 0 && slot_choice < INVENTORY_SLOTS &&
                             state->entities[player_idx].inventory[slot_choice].occupied &&
                             state->entities[player_idx].inventory[slot_choice].name[0] != '\0';
                sem_post(&state->global_mutex);

                if (valid) {
                    weapon_slot = slot_choice;
                } else {
                    std::printf("Invalid weapon slot. Try again.\n");
                }
            }
        }

        if (action == ActionType::USE_WEAPON) {
            target_idx = -1;
            while (target_idx < 0) {
                bool has_enemy = false;
                pthread_mutex_lock(&input_mutex);
                sem_wait(&state->global_mutex);
                std::printf("Choose target (alive enemy):\n");
                int total_entities = state->player_count + state->npc_count;
                for (int i = 0; i < total_entities; ++i) {
                    if (!state->entities[i].is_player && state->entities[i].is_alive) {
                        std::printf("  [%d] %s HP=%d\n", i, state->entities[i].name,
                                    state->entities[i].hp);
                        has_enemy = true;
                    }
                }
                sem_post(&state->global_mutex);

                if (!has_enemy) {
                    std::printf("No alive enemies.\n");
                    pthread_mutex_unlock(&input_mutex);
                    action = ActionType::SKIP;
                    break;
                }

                std::printf("> ");
                bool got_target = readLine(input, sizeof(input));
                pthread_mutex_unlock(&input_mutex);
                if (!got_target) {
                    continue;
                }
                int target_choice = std::atoi(input);

                sem_wait(&state->global_mutex);
                bool valid = target_choice >= 0 && target_choice < total_entities &&
                             !state->entities[target_choice].is_player && state->entities[target_choice].is_alive;
                sem_post(&state->global_mutex);

                if (valid) {
                    target_idx = target_choice;
                } else {
                    std::printf("Invalid target. Choose an enemy.\n");
                }
            }
        }

        if (action == ActionType::SWAP_IN) {
            while (weapon_slot < 0) {
                pthread_mutex_lock(&input_mutex);
                sem_wait(&state->global_mutex);
                int long_term_count = state->entities[player_idx].long_term_count;
                if (long_term_count <= 0) {
                    sem_post(&state->global_mutex);
                    std::printf("Long-term storage is empty.\n");
                    pthread_mutex_unlock(&input_mutex);
                    action = ActionType::SKIP;
                    break;
                }

                std::printf("Long-term storage:\n");
                for (int i = 0; i < long_term_count; ++i) {
                    const Weapon& slot = state->entities[player_idx].long_term[i];
                    if (slot.name[0] != '\0') {
                        std::printf("  [%d] %s (DMG=%d)\n", i, slot.name, slot.damage);
                    } else {
                        std::printf("  [%d] <empty>\n", i);
                    }
                }
                sem_post(&state->global_mutex);

                std::printf("Choose long-term index to swap in:\n> ");
                bool got_swap = readLine(input, sizeof(input));
                pthread_mutex_unlock(&input_mutex);
                if (!got_swap) {
                    continue;
                }
                int slot_choice = std::atoi(input);

                sem_wait(&state->global_mutex);
                bool valid = slot_choice >= 0 && slot_choice < state->entities[player_idx].long_term_count;
                sem_post(&state->global_mutex);

                if (valid) {
                    weapon_slot = slot_choice;
                } else {
                    std::printf("Invalid long-term index. Try again.\n");
                }
            }
        }

        sem_wait(&state->action_mutex);
        state->action_buffer.action = action;
        state->action_buffer.actor_idx = player_idx;
        state->action_buffer.target_idx = target_idx;
        state->action_buffer.weapon_slot = weapon_slot;
        sem_post(&state->action_mutex);

        sem_wait(&state->global_mutex);
        state->entities[player_idx].action_ready = true;
        sem_post(&state->global_mutex);

        if (action == ActionType::QUIT) {
            // Use the Arbiter PID stored in shared memory rather than getppid(),
            // because process tree relationships may vary and getppid() may not
            // always return the correct Arbiter PID.
            kill(state->arbiter_pid, SIGTERM);
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