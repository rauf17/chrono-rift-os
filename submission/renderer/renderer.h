#pragma once

// renderer.h  –  Partner A
// Owns: SFML window, background, entity rows (HP/stamina bars, status icons),
//       action log panel, ultimate overlay.
// Does NOT own: sprite sheet UV rects, entity sprite selection, weapon icons.
//               Those live in sprite_sheet.h / sprite_sheet.cpp (Partner B).

#include <SFML/Graphics.hpp>
#include <string>
#include "../shared/shared_state.h"
#include "sprite_sheet.h"   // Partner B's class – included here, owned there

// A plain-old-data snapshot of GlobalState that Renderer holds for one frame.
// Renderer takes a value copy of this so it never touches shared memory
// while drawing (no mutex needed during render calls).
struct RenderSnapshot {
    int  player_count;
    int  npc_count;
    bool game_running;
    bool ultimate_active;
    int  current_turn_idx;
    int  log_head;
    char log[ACTION_LOG_LINES][ACTION_LOG_WIDTH];

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
        // Inventory – only first 6 occupied weapons for icon strip
        struct WeaponSnap {
            char name[32];
            bool occupied;
            int  slot_size;
            bool is_artifact;
        } inventory[INVENTORY_SLOTS];
    } entities[MAX_ENTITIES];
};

// Capture a thread-safe snapshot of GlobalState.
// Caller must NOT hold global_mutex when calling this.
// This function locks global_mutex internally.
RenderSnapshot captureSnapshot(GlobalState* state);

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------
class Renderer {
public:
    // window_title   – caption shown in the OS title bar
    // assets_path    – path to the assets/ directory (e.g. "../assets")
    explicit Renderer(const std::string& assets_path,
                      const std::string& window_title = "Chrono Rift");

    // Returns false once the window has been closed by the user.
    bool isOpen() const;

    // Drain the SFML event queue; call once per frame before render().
    // Returns false if a close event was received.
    bool pollEvents();

    // Draw one complete frame from the given snapshot.
    // Safe to call from any thread that exclusively owns the RenderWindow
    // (i.e. the render thread). Never touches shared memory.
    void render(const RenderSnapshot& snap);

    // Gracefully close the window.
    void close();

private:
    // ---- helpers -----------------------------------------------------------
    void drawBackground();
    void drawEntityRow(const RenderSnapshot::EntitySnap& ent,
                       int entity_index,
                       float y,
                       bool  highlight_turn);
    void drawHpBar(float x, float y, float w, float h,
                   int hp, int max_hp);
    void drawStaminaBar(float x, float y, float w, float h,
                        float stamina, float max_stamina);
    void drawActionLog(const RenderSnapshot& snap);
    void drawUltimateOverlay();
    void drawPanelBackground(float x, float y, float w, float h,
                             sf::Color fill, float corner_radius = 6.f);

    // ---- members -----------------------------------------------------------
    sf::RenderWindow   m_window;
    sf::Texture        m_bg_texture;
    sf::Sprite         m_bg_sprite;
    sf::Font           m_font;          // BlazeCircuit – Partner A's text
    SpriteSheet        m_sprites;       // Partner B's sprite atlas

    // Reusable shapes (avoid per-frame heap allocs)
    sf::RectangleShape m_rect;
    sf::Text           m_text;

    // Layout constants (computed once in constructor from window size)
    float m_win_w;
    float m_win_h;
    float m_entity_panel_x;    // left edge of entity list panel
    float m_entity_panel_w;    // width of entity list panel
    float m_log_panel_x;
    float m_log_panel_y;
    float m_log_panel_w;
    float m_log_panel_h;
    float m_row_height;        // pixel height of one entity row
    unsigned int m_font_size_normal;
    unsigned int m_font_size_small;
};