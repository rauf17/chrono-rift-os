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
// LoginResult  –  returned by Renderer::runLoginScreen()
// ---------------------------------------------------------------------------
struct LoginResult {
    int  roll_number  = 0;
    int  player_count = 1;
    bool confirmed    = false;  // true = PLAY clicked, false = window closed
};

// ---------------------------------------------------------------------------
// EntityAnim
// ---------------------------------------------------------------------------
struct EntityAnim {
    bool  attacking     = false;
    float attack_dx     = 0.f;
    float attack_target = 0.f;
    float attack_t      = 0.f;
    bool  attack_return = false;

    bool  weapon_float          = false;
    char  weapon_float_name[32] = {};
    int   weapon_float_target   = -1;
    float weapon_float_t        = 0.f;

    bool  hit_flash   = false;
    float hit_flash_t = 0.f;

    bool  stun_flash   = false;
    float stun_flash_t = 0.f;
    int   stun_flashes = 0;

    float death_alpha    = 255.f;
    bool  dead_confirmed = false;

    float pulse_t = 0.f;
};

// ---------------------------------------------------------------------------
// GuiButton / GuiAction
// ---------------------------------------------------------------------------
struct GuiButton {
    sf::FloatRect bounds;
    std::string   label;
    bool          enabled = true;
    bool          hovered = false;
};

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

    bool isOpen() const;
    bool pollEvents();
    void render(const RenderSnapshot& snap);
    void close();

    // Blocking login screen – call before game starts.
    // Shows a beautiful overlay asking roll number + player count.
    // Returns when PLAY is clicked or window is closed.
    LoginResult runLoginScreen();

    bool pollGuiAction(GuiAction& out);
    void setPlayerTurn(int player_idx, const RenderSnapshot& snap);

    void triggerAttackAnim(int attacker_idx, int target_idx);
    void triggerWeaponFloat(int attacker_idx, int target_idx,
                            const char* weapon_name);
    void triggerHitFlash(int target_idx);
    void triggerStunFlash(int target_idx);
    void triggerDeath(int entity_idx);
    void showDropOffer(int player_idx, const char* weapon_name);
    void triggerWaveBanner(int wave_number);
    void stepAnimations(float dt_seconds);

private:
    // ---- login helpers ---------------------------------------------------- 
    void  drawLoginScreen(float dt);
    void  loginHandleTextEntered(sf::Uint32 unicode);
    void  loginHandleKeyPressed(sf::Keyboard::Key key);
    void  loginHandleClick(sf::Vector2f mp);
    bool  loginValidate(int& out_roll, int& out_players);

    // ---- game draw helpers -------------------------------------------------
    void drawBackground();
    void drawBattlefield(const RenderSnapshot& snap);
    void drawEntityCard(const RenderSnapshot::EntitySnap& ent,
                        int entity_index, sf::Vector2f pos, bool highlight,
                        const char* override_name = nullptr);
    void drawFloatingWeapons();
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
    void drawPanelBackground(float x, float y, float w, float h, sf::Color fill);
    void drawText(const std::string& str, float x, float y,
                  unsigned int size, sf::Color col,
                  sf::Color outline = sf::Color::Black);
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

    float m_win_w = 1600.f;
    float m_win_h = 900.f;

    std::array<EntityAnim,   MAX_ENTITIES> m_anims;
    std::array<sf::Vector2f, MAX_ENTITIES> m_entity_pos;

    // ---- Login state -------------------------------------------------------
    enum class LoginField { ROLL, PLAYERS, NONE };

    std::string m_roll_str;
    std::string m_players_str;
    LoginField  m_login_focus   = LoginField::ROLL;
    std::string m_login_err;        // empty = no error
    float       m_login_t     = 0.f; // anim time accumulator
    bool        m_login_play_hovered = false;

    // ---- Game state --------------------------------------------------------
    enum class InputPhase {
        NONE,
        ACTION_MENU,
        TARGET_SELECT,
        INVENTORY_SELECT,
        LONGTERM_SELECT,
        DROP_OFFER,
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

    char  m_drop_weapon_name[32] = {};
    int   m_drop_for_player      = -1;

    bool  m_wave_banner_active = false;
    float m_wave_banner_t      = 0.f;
    int   m_wave_banner_number = 1;

    float m_end_screen_t = 0.f;

    static constexpr unsigned int kFontLg = 26u;
    static constexpr unsigned int kFontMd = 19u;
    static constexpr unsigned int kFontSm = 15u;
};