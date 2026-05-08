#pragma once

#include <semaphore.h>
#include <sys/types.h>
#include <cstdint>

// This file defines the shared memory layout for the Chrono Rift game.
// It contains the GlobalState struct that is placed in a POSIX shared memory segment.
// The Arbiter process creates and initializes the shared memory.
// The HIP process (human interface) reads game state and writes actions.
// The ASP process (AI simulation) reads game state and simulates NPC actions.
// All access to shared memory must be protected by global_mutex for general state
// or action_mutex for action_buffer fields.
// Semaphores must be initialized with sem_init(..., 1, ...) to enable PTHREAD_PROCESS_SHARED.

const int MAX_PLAYERS = 4;
const int MAX_NPCS = 9;
const int MAX_ENTITIES = 13;
const int INVENTORY_SLOTS = 20;
const int LONG_TERM_SIZE = 50;
const int ACTION_LOG_LINES = 20;
const int ACTION_LOG_WIDTH = 128;
const int MAX_ARTIFACTS = 3;

struct Weapon {
    char name[32];       // Weapon name - read by all, written by Partner B
    int slot_size;       // Slots occupied - read by all, written by Partner B
    int damage;          // Damage value - read by Arbiter, written by Partner B
    bool is_artifact;    // Artifact flag - read by all, written by Partner B
    bool occupied;       // Slot occupied - read by all, written by Partner B
};

struct ArtifactEntry {
    char name[32];                // Artifact name - read by all, written by Partner B
    bool exists;                  // Exists in game - read by all, written by Partner B
    bool is_free;                 // Available to acquire - read by all, written by Partner B
    int held_by;                  // Holding entity index - read by all, written by Partner B
    bool waiting[MAX_ENTITIES];   // Waiting entities - read by all, written by Partner B
};

struct Entity {
    char name[32];                    // Entity name - read by all, written by Arbiter
    bool is_player;                   // Player flag - read by all, written by Arbiter
    bool is_alive;                    // Alive status - read by all, written by Arbiter
    int hp;                           // Current HP - read by all, written by Arbiter
    int max_hp;                       // Max HP - read by all, written by Arbiter
    int damage;                       // Base damage - read by Arbiter, written by Arbiter
    float speed;                      // Speed stat - read by Arbiter, written by Arbiter
    float stamina;                    // Current stamina - read by all, written by Arbiter
    float max_stamina;                // Max stamina - read by all, written by Arbiter
    bool is_my_turn;                  // Turn flag - read by HIP/ASP, written by Arbiter
    bool action_ready;                // Action ready - read by Arbiter, written by HIP/ASP
    bool is_stunned;                  // Stunned status - read by Arbiter, written by Partner B
    long stun_end_time;               // Stun end time - read by Arbiter, written by Partner B
    Weapon inventory[INVENTORY_SLOTS]; // Inventory - read by all, written by Partner B
    Weapon long_term[LONG_TERM_SIZE];  // Long-term storage - read by all, written by Partner B
    int long_term_count;              // Long-term count - read by all, written by Partner B
    bool holds_solar_core;            // Holds Solar Core - read by all, written by Partner B
    bool holds_lunar_blade;           // Holds Lunar Blade - read by all, written by Partner B
    bool holds_eclipse_relic;         // Holds Eclipse Relic - read by all, written by Partner B
};

enum class ActionType : int {
    NONE = 0,
    STRIKE = 1,
    EXHAUST = 2,
    USE_WEAPON = 3,
    SWAP_IN = 4,
    HEAL = 5,
    SKIP = 6,
    ULTIMATE = 7,
    STUN = 8,
    QUIT = 9
};

struct ActionBuffer {
    ActionType action;  // Action type - read by Arbiter, written by HIP/ASP
    int actor_idx;      // Actor index - read by Arbiter, written by HIP/ASP
    int target_idx;     // Target index - read by Arbiter, written by HIP/ASP
    int weapon_slot;    // Weapon slot - read by Arbiter, written by HIP/ASP
};

struct GlobalState {
    sem_t global_mutex;              // Global mutex - used by all processes
    sem_t action_mutex;              // Action mutex - used by all processes
    int roll_seed;                   // Roll seed - read by Arbiter, written by Arbiter
    int player_count;                // Player count - read by all, written by Arbiter
    int npc_count;                   // NPC count - read by all, written by Arbiter
    Entity entities[MAX_ENTITIES];   // Entities array - read by all, written by Arbiter/Partner B
    ActionBuffer action_buffer;      // Action buffer - read by Arbiter, written by HIP/ASP
    bool game_running;               // Game running - read by all, written by Arbiter
    int current_turn_idx;            // Current turn - read by all, written by Arbiter
    int enemies_killed;              // Enemies killed - read by all, written by Arbiter
    bool ultimate_active;            // Ultimate active - read by all, written by Partner B
    pid_t arbiter_pid;               // Arbiter PID - read by HIP/ASP, written by Arbiter
    ArtifactEntry artifacts[MAX_ARTIFACTS]; // Artifacts - read by all, written by Partner B
    char log[ACTION_LOG_LINES][ACTION_LOG_WIDTH]; // Action log - read by HIP, written by Arbiter
    int log_head;                    // Log head - read by Arbiter/HIP, written by Arbiter
    bool pending_drop_offer;         // Pending weapon drop offer - written by Arbiter, read by HIP
    char pending_drop_name[32];      // Name of the dropped weapon - written by Arbiter, read by HIP
    int pending_drop_for_player;     // Index of the player who should be offered the drop - written by Arbiter, read by HIP
};