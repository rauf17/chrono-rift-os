// shared/shared_state.h
// Shared memory layout for Chrono Rift.
// This struct is placed in a POSIX shared memory segment created by the Arbiter.
// All three processes map this same segment. All reads/writes must be protected
// by the global_mutex semaphore (or the action_mutex for action fields only).

#pragma once
#include <semaphore.h>
#include <cstdint>

// ──────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────

constexpr int MAX_PLAYERS       = 4;
constexpr int MAX_NPCS          = 9;
constexpr int MAX_ENTITIES      = MAX_PLAYERS + MAX_NPCS;
constexpr int INVENTORY_SLOTS   = 20;
constexpr int LONG_TERM_SIZE    = 50;
constexpr int ACTION_LOG_LINES  = 20;
constexpr int ACTION_LOG_WIDTH  = 128;
constexpr int MAX_ARTIFACTS     = 3;  // Solar Core, Lunar Blade, Eclipse Relic

// ──────────────────────────────────────────────
// Weapon definition (used by Partner B)
// ──────────────────────────────────────────────

struct Weapon {
    char  name[32];       // Weapon name string
    int   slot_size;      // Number of contiguous inventory slots it occupies
    int   damage;         // Damage output value
    bool  is_artifact;    // True for Solar Core and Lunar Blade
    bool  occupied;       // True if this slot in the inventory array holds a weapon
};

// ──────────────────────────────────────────────
// Artifact resource table entry (used by Partner B)
// ──────────────────────────────────────────────

struct ArtifactEntry {
    char  name[32];       // Artifact name
    bool  exists;         // False until Eclipse Relic is introduced at runtime
    bool  is_free;        // True if no one currently holds this artifact
    int   held_by;        // Entity index of the holder (-1 if free)
    bool  waiting[MAX_ENTITIES]; // True for each entity that is waiting to acquire this artifact
};

// ──────────────────────────────────────────────
// Entity (player or NPC)
// ──────────────────────────────────────────────

struct Entity {
    // Identity
    char  name[32];
    bool  is_player;      // True for human-controlled characters
    bool  is_alive;

    // Combat stats (initialized by Arbiter using roll number seed)
    int   hp;
    int   max_hp;
    int   damage;
    float speed;
    float stamina;
    float max_stamina;

    // Turn control (written by Arbiter, read by HIP and ASP)
    bool  is_my_turn;     // Arbiter sets this true when this entity should act
    bool  action_ready;   // HIP/ASP sets this true after writing action fields below
    bool  is_stunned;     // Partner B sets this true when stun mechanic is applied
    long  stun_end_time;  // Unix timestamp when stun expires

    // Inventory (managed by Partner B's space allocator)
    Weapon inventory[INVENTORY_SLOTS];  // Primary 20-slot linear inventory
    Weapon long_term[LONG_TERM_SIZE];   // Long-term storage for swapped-out weapons
    int    long_term_count;             // Number of items currently in long-term storage

    // Artifact holding (managed by Partner B)
    bool   holds_solar_core;
    bool   holds_lunar_blade;
    bool   holds_eclipse_relic;
};

// ──────────────────────────────────────────────
// Action buffer (written by HIP/ASP, consumed by Arbiter)
// ──────────────────────────────────────────────

enum class ActionType : int {
    NONE      = 0,
    STRIKE    = 1,   // Attack: reduce target HP by actor damage
    EXHAUST   = 2,   // Attack: reduce target Stamina by actor damage (player only)
    USE_WEAPON= 3,   // Attack using equipped weapon
    SWAP_IN   = 4,   // Bring weapon from long-term storage
    HEAL      = 5,   // Restore 10% of own HP
    SKIP      = 6,   // Stamina set to 50% instead of 0
    ULTIMATE  = 7,   // Requires Solar Core + Lunar Blade (player only)
    STUN      = 8,   // High-tier attack that triggers SIGUSR1 to ASP (player only)
    QUIT      = 9    // Player chose to quit; HIP sends SIGTERM to Arbiter
};

struct ActionBuffer {
    ActionType action;      // The chosen action type
    int        actor_idx;   // Index into entities[] of the acting entity
    int        target_idx;  // Index into entities[] of the target (-1 if no target)
    int        weapon_slot; // Inventory slot index for USE_WEAPON or SWAP_IN (-1 otherwise)
};

// ──────────────────────────────────────────────
// Global state (the entire shared memory segment)
// ──────────────────────────────────────────────

struct GlobalState {
    // Synchronization — must be initialized with PTHREAD_PROCESS_SHARED
    sem_t  global_mutex;   // Protects all reads and writes to this struct
    sem_t  action_mutex;   // Protects the action_buffer fields specifically

    // Roll number seed (set once by Arbiter at startup)
    int    roll_seed;

    // Entity array — players first, then NPCs
    int    player_count;   // 1 to 4 (chosen at startup)
    int    npc_count;      // 2 to 9 (randomized using roll_seed)
    Entity entities[MAX_ENTITIES];

    // Action buffer — single pending action slot (serial execution)
    ActionBuffer action_buffer;

    // Game flow control (written by Arbiter)
    bool   game_running;       // False when the game should exit
    int    current_turn_idx;   // Index into entities[] of whose turn it is right now
    int    enemies_killed;     // Counter toward the 10-kill win condition
    bool   ultimate_active;    // True during the 10-second Ultimate Ability window

    // Artifact resource table (Partner B fills this in)
    ArtifactEntry artifacts[MAX_ARTIFACTS];

    // Action log (circular buffer for the ncurses renderer)
    char   log[ACTION_LOG_LINES][ACTION_LOG_WIDTH];
    int    log_head;           // Index of the next line to write
};