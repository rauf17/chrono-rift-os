#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <array>
#include "../shared/shared_state.h"
#include "sprite_sheet.h"

// ---------------------------------------------------------------------------
// RenderSnapshot
// ---------------------------------------------------------------------------
struct RenderSnapshot {
    int  player_count;
    int  npc_count;
    bool game_running;
    bool ultimate_active;
    int  current_turn_idx;
    int  log_head;
    char log[ACTION_LOG_LINES][ACTION_LOG_WIDTH];

    // Wave / end-game state (added for wave system)
    int  wave_number    = 1;
    int  enemies_killed = 0;
    bool game_won       = false;
    bool game_lost      = false;

    struct EntitySnap {
        char  name[32];
        bool  is_player;
        bool  is_alive;
        bool  is_my_turn;
        bool  is_stunned;
        int   hp;
        int   max_hp;
        float stamina;
        float max_stamina;
        struct WeaponSnap {
            char name[32];
            bool occupied;
            int  slot_size;
            bool is_artifact;
        } inventory[INVENTORY_SLOTS];
        struct WeaponSnap long_term[LONG_TERM_SIZE];
        int  long_term_count;
        bool holds_solar_core;
        bool holds_lunar_blade;
    } entities[MAX_ENTITIES];
};

RenderSnapshot captureSnapshot(GlobalState* state);

// ---------------------------------------------------------------------------
// EntityAnim  –  per-entity animation state
// ---------------------------------------------------------------------------
struct EntityAnim {

    // --- Slide-forward attack -----------------------------------------------
    bool  attacking     = false;
    float attack_dx     = 0.f;      // current x pixel offset
    float attack_target = 0.f;      // maximum slide distance
    float attack_t      = 0.f;      // 0..1 progress timer
    bool  attack_return = false;    // true while sliding back

    // --- Floating weapon icon (Use Weapon action) ---------------------------
    // Active while the attacker is mid-slide.
    // The icon travels from the attacker card toward the target card,
    // reaching the midpoint at attack_t==1, then disappearing on return.
    bool  weapon_float        = false;
    char  weapon_float_name[32] = {};   // which weapon icon to draw
    int   weapon_float_target = -1;     // target entity index for position lerp
    float weapon_float_t      = 0.f;    // 0=at attacker  1=at midpoint

    // --- Hit flash (red tint) -----------------------------------------------
    bool  hit_flash   = false;
    float hit_flash_t = 0.f;   // 1 = full red, counts down to 0

    // --- Stun flash burst ---------------------------------------------------
    // Flashes the stunned icon over the target 3 times (~0.9 s total).
    bool  stun_flash   = false;
    float stun_flash_t = 0.f;   // counts 0 → 1 per flash cycle
    int   stun_flashes = 0;     // flashes remaining (start at 3, counts down)

    // --- Death fade ---------------------------------------------------------
    float death_alpha    = 255.f;
    bool  dead_confirmed = false;

    // --- Turn pulse (sprite scale breathe) ----------------------------------
    float pulse_t = 0.f;
};

// ---------------------------------------------------------------------------
// GuiButton
// ---------------------------------------------------------------------------
struct GuiButton {
    sf::FloatRect bounds;
    std::string   label;
    bool          enabled = true;
    bool          hovered = false;
};

// ---------------------------------------------------------------------------
// GuiAction  –  completed player choice, passed back to renderThreadSFML
// ---------------------------------------------------------------------------
struct GuiAction {
    bool       ready       = false;
    ActionType action      = ActionType::NONE;
    int        target_idx  = -1;
    int        weapon_slot = -1;
};

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------
class Renderer {
public:
    explicit Renderer(const std::string& assets_path,
                      const std::string& window_title = "Chrono Rift");

    bool isOpen()    const;
    bool pollEvents();
    void render(const RenderSnapshot& snap);
    void close();

    // Returns true once per turn when the player has committed an action.
    bool pollGuiAction(GuiAction& out);

    // Call when a player's turn starts (player_idx >= 0) or ends (-1).
    void setPlayerTurn(int player_idx, const RenderSnapshot& snap);

    // Animation triggers – called from renderThreadSFML
    void triggerAttackAnim(int attacker_idx, int target_idx);
    void triggerWeaponFloat(int attacker_idx, int target_idx,
                            const char* weapon_name);
    void triggerHitFlash(int target_idx);
    void triggerStunFlash(int target_idx);
    void triggerDeath(int entity_idx);

    // Show a weapon drop pickup dialog for a player.
    void showDropOffer(int player_idx, const char* weapon_name);

    // Trigger the "Wave N incoming!" banner (called when wave_number changes).
    void triggerWaveBanner(int wave_number);

    void stepAnimations(float dt_seconds);

private:
    // ---- draw helpers ------------------------------------------------------
    void drawBackground();
    void drawBattlefield(const RenderSnapshot& snap);
    void drawEntityCard(const RenderSnapshot::EntitySnap& ent,
                        int entity_index, sf::Vector2f pos, bool highlight,
                        const char* override_name = nullptr);
    void drawFloatingWeapons();

    // Draw helpers (private)
    void drawDropOfferDialog();
    void drawHpBar(float x, float y, float w, float h, int hp, int max_hp);
    void drawStaminaBar(float x, float y, float w, float h,
                        float stamina, float max_stamina);
    void drawActionMenu(const RenderSnapshot& snap);
    void drawTargetOverlay(const RenderSnapshot& snap);
    void drawInventoryMenu(const RenderSnapshot& snap);
    void drawLongTermMenu(const RenderSnapshot& snap);
    void drawUltimateOverlay();
    void drawStatusBar(const RenderSnapshot& snap);
    void drawPanelBackground(float x, float y, float w, float h,
                             sf::Color fill);
    void drawText(const std::string& str, float x, float y,
                  unsigned int size, sf::Color col,
                  sf::Color outline = sf::Color::Black);

    // Wave & end-screen overlays
    void drawWaveBanner();
    void drawVictoryScreen();
    void drawGameOverScreen();

    // ---- input helpers -----------------------------------------------------
    void handleMouseMove(sf::Vector2f mp);
    void handleMouseClick(sf::Vector2f mp, const RenderSnapshot& snap);
    void buildActionButtons(const RenderSnapshot::EntitySnap& actor);
    void buildTargetButtons(const RenderSnapshot& snap);
    void buildInventoryButtons(const RenderSnapshot::EntitySnap& actor);
    void buildLongTermButtons(const RenderSnapshot::EntitySnap& actor);

    // ---- members -----------------------------------------------------------
    sf::RenderWindow   m_window;
    sf::Texture        m_bg_texture;
    sf::Sprite         m_bg_sprite;
    sf::Font           m_font;
    SpriteSheet        m_sprites;
    sf::RectangleShape m_rect;
    sf::Text           m_text;
    sf::Clock          m_clock;

    float m_win_w = 1280.f;
    float m_win_h = 720.f;

    std::array<EntityAnim,   MAX_ENTITIES> m_anims;
    std::array<sf::Vector2f, MAX_ENTITIES> m_entity_pos;  // card centre positions

    enum class InputPhase {
        NONE,
        ACTION_MENU,
        TARGET_SELECT,
        INVENTORY_SELECT,
        LONGTERM_SELECT,
        DROP_OFFER,       // showing pickup Y/N dialog for a weapon drop
    };

    InputPhase  m_phase          = InputPhase::NONE;
    int         m_active_player  = -1;
    ActionType  m_pending_action = ActionType::NONE;

    std::vector<GuiButton> m_action_buttons;
    std::vector<GuiButton> m_target_buttons;
    std::vector<GuiButton> m_inventory_buttons;
    std::vector<GuiButton> m_longterm_buttons;

    GuiAction      m_pending_gui_action;
    RenderSnapshot m_cached_snap;

    // Drop offer dialog state
    char  m_drop_weapon_name[32] = {};
    int   m_drop_for_player      = -1;

    // Wave banner state
    bool  m_wave_banner_active = false;
    float m_wave_banner_t      = 0.f;  // counts up from 0; banner visible while < 3.0 s
    int   m_wave_banner_number = 1;

    // End-screen animation time
    float m_end_screen_t = 0.f;

    static constexpr unsigned int kFontLg = 26u;
    static constexpr unsigned int kFontMd = 19u;
    static constexpr unsigned int kFontSm = 15u;
};