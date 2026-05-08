// renderer.cpp  –  Partner A
// Implements: window management, background, entity rows, HP/stamina bars,
//             status icons, action log panel, ultimate overlay.

#include "renderer.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <semaphore.h>

// ---------------------------------------------------------------------------
// captureSnapshot – call from the render/game thread, locks global_mutex once
// ---------------------------------------------------------------------------
RenderSnapshot captureSnapshot(GlobalState* state) {
    RenderSnapshot snap{};
    sem_wait(&state->global_mutex);

    snap.player_count   = state->player_count;
    snap.npc_count      = state->npc_count;
    snap.game_running   = state->game_running;
    snap.ultimate_active = state->ultimate_active;
    snap.current_turn_idx = state->current_turn_idx;
    snap.log_head       = state->log_head;

    for (int i = 0; i < ACTION_LOG_LINES; ++i)
        std::memcpy(snap.log[i], state->log[i], ACTION_LOG_WIDTH);

    int total = state->player_count + state->npc_count;
    for (int i = 0; i < total && i < MAX_ENTITIES; ++i) {
        const Entity& src = state->entities[i];
        auto& dst = snap.entities[i];
        std::strncpy(dst.name, src.name, 32);
        dst.is_player    = src.is_player;
        dst.is_alive     = src.is_alive;
        dst.is_my_turn   = src.is_my_turn;
        dst.is_stunned   = src.is_stunned;
        dst.hp           = src.hp;
        dst.max_hp       = src.max_hp;
        dst.stamina      = src.stamina;
        dst.max_stamina  = src.max_stamina;
        for (int s = 0; s < INVENTORY_SLOTS; ++s) {
            std::strncpy(dst.inventory[s].name, src.inventory[s].name, 32);
            dst.inventory[s].occupied    = src.inventory[s].occupied;
            dst.inventory[s].slot_size   = src.inventory[s].slot_size;
            dst.inventory[s].is_artifact = src.inventory[s].is_artifact;
        }
    }

    sem_post(&state->global_mutex);
    return snap;
}

// ---------------------------------------------------------------------------
// Color palette  (Chrono Rift dark-fantasy theme)
// ---------------------------------------------------------------------------
namespace Color {
    static const sf::Color BG_PANEL    {10,  12,  20,  210}; // near-black, translucent
    static const sf::Color BG_PANEL2   {18,  22,  38,  230}; // slightly lighter panel
    static const sf::Color BORDER      {80,  60,  30,  255}; // aged gold border
    static const sf::Color PLAYER_NAME {200, 230, 255, 255}; // icy blue
    static const sf::Color ENEMY_NAME  {255, 160, 100, 255}; // ember orange
    static const sf::Color DEAD_NAME   {90,  90,  90,  200}; // greyed-out
    static const sf::Color HP_FULL     {200,  40,  40,  255}; // crimson
    static const sf::Color HP_LOW      {255, 120,   0,  255}; // orange (< 30 %)
    static const sf::Color HP_BG       {40,  10,  10,  200};
    static const sf::Color STAM_FULL   {230, 190,  30,  255}; // gold
    static const sf::Color STAM_BG     {40,  35,   5,  200};
    static const sf::Color TURN_HL     {255, 230, 100,  35}; // row highlight
    static const sf::Color LOG_BG      {8,   10,  18,  220};
    static const sf::Color LOG_TEXT    {180, 200, 220, 220};
    static const sf::Color LOG_HEAD    {220, 200, 100, 255}; // "Action Log" label
    static const sf::Color ULTIMATE    {255, 200,  50, 200}; // golden pulse overlay
    static const sf::Color STUN_CLR    {180, 255, 100, 255}; // acid-green STUNNED text
    static const sf::Color WHITE       {255, 255, 255, 255};
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Renderer::Renderer(const std::string& assets_path, const std::string& window_title)
    : m_sprites(assets_path + "/sprites/sprites.png")
{
    // Window: 1280×720, vsync on
    m_window.create(sf::VideoMode(1280, 720), window_title,
                    sf::Style::Titlebar | sf::Style::Close);
    m_window.setVerticalSyncEnabled(true);
    m_window.setFramerateLimit(60);

    m_win_w = static_cast<float>(m_window.getSize().x);
    m_win_h = static_cast<float>(m_window.getSize().y);

    // ---- Load background texture ----------------------------------------
    std::string bg_path = assets_path + "/backgrounds/dungeon.png";
    if (!m_bg_texture.loadFromFile(bg_path)) {
        // fallback: solid dark colour – draw nothing special
    }
    m_bg_texture.setRepeated(false);
    m_bg_sprite.setTexture(m_bg_texture);

    // Scale background to fill window while keeping aspect ratio
    sf::Vector2u tex_sz = m_bg_texture.getSize();
    if (tex_sz.x > 0 && tex_sz.y > 0) {
        float sx = m_win_w  / static_cast<float>(tex_sz.x);
        float sy = m_win_h  / static_cast<float>(tex_sz.y);
        float sc = std::max(sx, sy);
        m_bg_sprite.setScale(sc, sc);
        // Centre it
        float off_x = (m_win_w  - tex_sz.x * sc) / 2.f;
        float off_y = (m_win_h  - tex_sz.y * sc) / 2.f;
        m_bg_sprite.setPosition(off_x, off_y);
    }

    // ---- Load font ---------------------------------------------------------
    std::string font_path = assets_path + "/fonts/BlazeCircuit.ttf";
    if (!m_font.loadFromFile(font_path)) {
        // try fallback name as shipped
        m_font.loadFromFile(assets_path + "/fonts/BlazeCircuitRegular.ttf");
    }
    m_text.setFont(m_font);

    // ---- Layout ------------------------------------------------------------
    // Left panel: entity list  (55 % of width)
    // Right panel: action log  (44 % of width, small gap in between)
    m_entity_panel_x = 12.f;
    m_entity_panel_w = m_win_w * 0.54f;

    m_log_panel_x = m_entity_panel_x + m_entity_panel_w + 8.f;
    m_log_panel_y = 40.f;
    m_log_panel_w = m_win_w - m_log_panel_x - 12.f;
    m_log_panel_h = m_win_h - m_log_panel_y - 12.f;

    m_row_height = 48.f;
    m_font_size_normal = 16u;
    m_font_size_small  = 12u;
}

// ---------------------------------------------------------------------------
bool Renderer::isOpen() const { return m_window.isOpen(); }

bool Renderer::pollEvents() {
    sf::Event event{};
    while (m_window.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            m_window.close();
            return false;
        }
    }
    return true;
}

void Renderer::close() { m_window.close(); }

// ---------------------------------------------------------------------------
// render() – main per-frame entry point
// ---------------------------------------------------------------------------
void Renderer::render(const RenderSnapshot& snap) {
    m_window.clear(sf::Color(8, 10, 20));

    // 1. Background
    drawBackground();

    // 2. Entity panel backdrop
    drawPanelBackground(m_entity_panel_x - 4.f, 36.f,
                        m_entity_panel_w + 8.f,
                        m_win_h - 48.f,
                        Color::BG_PANEL);

    // 3. Header label
    m_text.setFont(m_font);
    m_text.setCharacterSize(22u);
    m_text.setFillColor(Color::BORDER);
    m_text.setString("CHRONO  RIFT");
    m_text.setPosition(m_entity_panel_x + 4.f, 10.f);
    m_window.draw(m_text);

    // 4. Entity rows
    int total = snap.player_count + snap.npc_count;
    float row_y = 50.f;
    for (int i = 0; i < total && i < MAX_ENTITIES; ++i) {
        bool hl = snap.entities[i].is_my_turn;
        drawEntityRow(snap.entities[i], i, row_y, hl);
        row_y += m_row_height;
    }

    // 5. Action log panel
    drawActionLog(snap);

    // 6. Ultimate overlay (on top of everything)
    if (snap.ultimate_active)
        drawUltimateOverlay();

    m_window.display();
}

// ---------------------------------------------------------------------------
// drawBackground
// ---------------------------------------------------------------------------
void Renderer::drawBackground() {
    // Draw dungeon background at reduced opacity so entity UI stays legible
    sf::Uint8 alpha = 140;
    m_bg_sprite.setColor(sf::Color(255, 255, 255, alpha));
    m_window.draw(m_bg_sprite);
}

// ---------------------------------------------------------------------------
// drawEntityRow
// ---------------------------------------------------------------------------
void Renderer::drawEntityRow(const RenderSnapshot::EntitySnap& ent,
                              int entity_index,
                              float y,
                              bool  highlight_turn)
{
    float x = m_entity_panel_x;
    float row_w = m_entity_panel_w;

    // Row highlight for the active entity
    if (highlight_turn) {
        m_rect.setSize({row_w, m_row_height - 2.f});
        m_rect.setPosition(x, y);
        m_rect.setFillColor(Color::TURN_HL);
        m_rect.setOutlineThickness(1.5f);
        m_rect.setOutlineColor(sf::Color(255, 230, 100, 120));
        m_window.draw(m_rect);
    }

    // ---- Sprite (Partner B draws it; we provide position) ------------------
    // Sprite is 40×40 px, vertically centred in the row
    float sprite_x = x + 2.f;
    float sprite_y = y + (m_row_height - 40.f) / 2.f;
    m_sprites.drawEntity(m_window, entity_index, ent.is_player,
                         {sprite_x, sprite_y});

    // ---- Turn arrow icon ---------------------------------------------------
    float icon_offset = 44.f;
    if (highlight_turn) {
        m_sprites.drawStatusIcon(m_window, "turn",
                                 {x + icon_offset, sprite_y});
        icon_offset += 22.f;
    }

    // ---- Name --------------------------------------------------------------
    float name_x = x + icon_offset + 4.f;
    m_text.setCharacterSize(m_font_size_normal);
    if (!ent.is_alive) {
        m_text.setFillColor(Color::DEAD_NAME);
        m_text.setString(std::string(ent.name) + " [DEAD]");
    } else if (ent.is_player) {
        m_text.setFillColor(Color::PLAYER_NAME);
        m_text.setString(ent.name);
    } else {
        m_text.setFillColor(Color::ENEMY_NAME);
        m_text.setString(ent.name);
    }
    m_text.setPosition(name_x, y + 4.f);
    m_window.draw(m_text);

    if (!ent.is_alive) return;   // dead entities: name only, skip bars

    // ---- HP bar ------------------------------------------------------------
    float bar_x  = name_x;
    float bar_y  = y + 24.f;
    float bar_w  = 160.f;
    float bar_h  = 8.f;
    drawHpBar(bar_x, bar_y, bar_w, bar_h, ent.hp, ent.max_hp);

    // HP numeric text  (e.g. "412 / 800")
    char hp_buf[32];
    std::snprintf(hp_buf, sizeof(hp_buf), "%d/%d", ent.hp, ent.max_hp);
    m_text.setCharacterSize(m_font_size_small);
    m_text.setFillColor(sf::Color(200, 200, 200, 200));
    m_text.setString(hp_buf);
    m_text.setPosition(bar_x + bar_w + 5.f, bar_y - 2.f);
    m_window.draw(m_text);

    // ---- Stamina bar -------------------------------------------------------
    float stam_x = bar_x + bar_w + 70.f;
    float stam_w = 120.f;
    drawStaminaBar(stam_x, bar_y, stam_w, bar_h, ent.stamina, ent.max_stamina);

    // Stamina %
    int stam_pct = (ent.max_stamina > 0.f)
                   ? static_cast<int>(ent.stamina / ent.max_stamina * 100.f)
                   : 0;
    char stam_buf[16];
    std::snprintf(stam_buf, sizeof(stam_buf), "%d%%", stam_pct);
    m_text.setCharacterSize(m_font_size_small);
    m_text.setFillColor(Color::STAM_FULL);
    m_text.setString(stam_buf);
    m_text.setPosition(stam_x + stam_w + 4.f, bar_y - 2.f);
    m_window.draw(m_text);

    // ---- STUNNED icon + label ----------------------------------------------
    if (ent.is_stunned) {
        float stun_x = stam_x + stam_w + 40.f;
        m_sprites.drawStatusIcon(m_window, "stunned", {stun_x, sprite_y});
        m_text.setCharacterSize(m_font_size_small);
        m_text.setFillColor(Color::STUN_CLR);
        m_text.setString("STUNNED");
        m_text.setPosition(stun_x + 22.f, y + 4.f);
        m_window.draw(m_text);
    }

    // ---- Weapon icon strip (first 4 occupied weapons) ----------------------
    float wicon_x = stam_x + stam_w + (ent.is_stunned ? 110.f : 40.f);
    int drawn = 0;
    for (int s = 0; s < INVENTORY_SLOTS && drawn < 4; ++s) {
        const auto& slot = ent.inventory[s];
        if (!slot.occupied || slot.name[0] == '\0') continue;
        // Only draw at the "head" slot (slot_size > 0)
        if (slot.slot_size <= 0) continue;
        m_sprites.drawWeaponIcon(m_window, slot.name,
                                 {wicon_x + drawn * 22.f, sprite_y + 4.f});
        ++drawn;
    }
}

// ---------------------------------------------------------------------------
// drawHpBar
// ---------------------------------------------------------------------------
void Renderer::drawHpBar(float x, float y, float w, float h,
                          int hp, int max_hp)
{
    // Background
    m_rect.setSize({w, h});
    m_rect.setPosition(x, y);
    m_rect.setFillColor(Color::HP_BG);
    m_rect.setOutlineThickness(1.f);
    m_rect.setOutlineColor(sf::Color(80, 20, 20, 180));
    m_window.draw(m_rect);

    // Fill
    float ratio = (max_hp > 0) ? std::max(0.f, std::min(1.f, static_cast<float>(hp) / max_hp)) : 0.f;
    bool  low   = ratio < 0.30f;
    m_rect.setSize({w * ratio, h});
    m_rect.setFillColor(low ? Color::HP_LOW : Color::HP_FULL);
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);
}

// ---------------------------------------------------------------------------
// drawStaminaBar
// ---------------------------------------------------------------------------
void Renderer::drawStaminaBar(float x, float y, float w, float h,
                               float stamina, float max_stamina)
{
    m_rect.setSize({w, h});
    m_rect.setPosition(x, y);
    m_rect.setFillColor(Color::STAM_BG);
    m_rect.setOutlineThickness(1.f);
    m_rect.setOutlineColor(sf::Color(60, 50, 10, 180));
    m_window.draw(m_rect);

    float ratio = (max_stamina > 0.f)
                  ? std::max(0.f, std::min(1.f, stamina / max_stamina))
                  : 0.f;
    m_rect.setSize({w * ratio, h});
    m_rect.setFillColor(Color::STAM_FULL);
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);
}

// ---------------------------------------------------------------------------
// drawActionLog
// ---------------------------------------------------------------------------
void Renderer::drawActionLog(const RenderSnapshot& snap) {
    // Panel background
    drawPanelBackground(m_log_panel_x, m_log_panel_y,
                        m_log_panel_w, m_log_panel_h,
                        Color::LOG_BG);

    // "Action Log" heading
    m_text.setCharacterSize(18u);
    m_text.setFillColor(Color::LOG_HEAD);
    m_text.setString("Action Log");
    m_text.setPosition(m_log_panel_x + 10.f, m_log_panel_y + 6.f);
    m_window.draw(m_text);

    // Separator line
    sf::RectangleShape sep({m_log_panel_w - 20.f, 1.f});
    sep.setPosition(m_log_panel_x + 10.f, m_log_panel_y + 28.f);
    sep.setFillColor(Color::BORDER);
    m_window.draw(sep);

    // Last 10 log entries (newest at bottom)
    const int kLines = 10;
    float line_h = (m_log_panel_h - 36.f) / static_cast<float>(kLines);
    m_text.setCharacterSize(m_font_size_small);

    for (int i = 0; i < kLines; ++i) {
        // index into circular buffer: newest entry = log_head-1, oldest shown = log_head-10
        int buf_idx = (snap.log_head - kLines + i + ACTION_LOG_LINES) % ACTION_LOG_LINES;
        const char* entry = snap.log[buf_idx];
        if (entry[0] == '\0') continue;

        // Fade older entries
        sf::Uint8 alpha = static_cast<sf::Uint8>(120 + i * 13);
        m_text.setFillColor(sf::Color(Color::LOG_TEXT.r, Color::LOG_TEXT.g,
                                      Color::LOG_TEXT.b, alpha));
        m_text.setString(entry);
        float ty = m_log_panel_y + 34.f + i * line_h;
        m_text.setPosition(m_log_panel_x + 10.f, ty);
        m_window.draw(m_text);
    }
}

// ---------------------------------------------------------------------------
// drawUltimateOverlay
// ---------------------------------------------------------------------------
void Renderer::drawUltimateOverlay() {
    // Full-screen golden vignette
    sf::RectangleShape overlay({m_win_w, m_win_h});
    overlay.setPosition(0.f, 0.f);
    overlay.setFillColor(sf::Color(255, 180, 20, 30));
    m_window.draw(overlay);

    // Animated pulsing border (4 thick rects along edges)
    static float pulse_t = 0.f;
    pulse_t += 0.05f;
    sf::Uint8 border_alpha = static_cast<sf::Uint8>(
        100.f + 80.f * std::abs(std::sin(pulse_t)));

    sf::Color bc(255, 200, 50, border_alpha);
    float bw = 6.f;
    // Top
    sf::RectangleShape b({m_win_w, bw});
    b.setFillColor(bc);
    b.setPosition(0.f, 0.f); m_window.draw(b);
    // Bottom
    b.setPosition(0.f, m_win_h - bw); m_window.draw(b);
    // Left
    b.setSize({bw, m_win_h});
    b.setPosition(0.f, 0.f); m_window.draw(b);
    // Right
    b.setPosition(m_win_w - bw, 0.f); m_window.draw(b);

    // "ULTIMATE ACTIVE" label
    m_text.setCharacterSize(26u);
    m_text.setFillColor(sf::Color(255, 230, 80, border_alpha));
    m_text.setString("ULTIMATE ACTIVE");
    sf::FloatRect tb = m_text.getLocalBounds();
    m_text.setPosition((m_win_w - tb.width) / 2.f, 8.f);
    m_window.draw(m_text);

    // Ultimate swirl icon (centre-top, drawn by Partner B)
    m_sprites.drawStatusIcon(m_window, "ultimate",
                             {(m_win_w - 40.f) / 2.f - 50.f, 8.f});
}

// ---------------------------------------------------------------------------
// drawPanelBackground  (rounded rect approximated with a plain rect + border)
// ---------------------------------------------------------------------------
void Renderer::drawPanelBackground(float x, float y, float w, float h,
                                    sf::Color fill, float /*corner_radius*/)
{
    // Main fill
    m_rect.setSize({w, h});
    m_rect.setPosition(x, y);
    m_rect.setFillColor(fill);
    m_rect.setOutlineThickness(1.5f);
    m_rect.setOutlineColor(Color::BORDER);
    m_window.draw(m_rect);
}