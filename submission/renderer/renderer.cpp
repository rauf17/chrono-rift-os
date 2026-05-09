#include "renderer.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <semaphore.h>

// ---------------------------------------------------------------------------
// captureSnapshot
// ---------------------------------------------------------------------------
RenderSnapshot captureSnapshot(GlobalState* state) {
    RenderSnapshot snap{};
    sem_wait(&state->global_mutex);

    snap.player_count     = state->player_count;
    snap.npc_count        = state->npc_count;
    snap.game_running     = state->game_running;
    snap.ultimate_active  = state->ultimate_active;
    snap.current_turn_idx = state->current_turn_idx;
    snap.log_head         = state->log_head;

    for (int i = 0; i < ACTION_LOG_LINES; ++i)
        std::memcpy(snap.log[i], state->log[i], ACTION_LOG_WIDTH);

    int total = state->player_count + state->npc_count;
    for (int i = 0; i < total && i < MAX_ENTITIES; ++i) {
        const Entity& src = state->entities[i];
        auto& dst = snap.entities[i];
        std::strncpy(dst.name, src.name, 32);
        dst.is_player         = src.is_player;
        dst.is_alive          = src.is_alive;
        dst.is_my_turn        = src.is_my_turn;
        dst.is_stunned        = src.is_stunned;
        dst.hp                = src.hp;
        dst.max_hp            = src.max_hp;
        dst.stamina           = src.stamina;
        dst.max_stamina       = src.max_stamina;
        dst.long_term_count   = src.long_term_count;
        dst.holds_solar_core  = src.holds_solar_core;
        dst.holds_lunar_blade = src.holds_lunar_blade;
        for (int s = 0; s < INVENTORY_SLOTS; ++s) {
            std::strncpy(dst.inventory[s].name, src.inventory[s].name, 32);
            dst.inventory[s].occupied    = src.inventory[s].occupied;
            dst.inventory[s].slot_size   = src.inventory[s].slot_size;
            dst.inventory[s].is_artifact = src.inventory[s].is_artifact;
        }
        for (int s = 0; s < LONG_TERM_SIZE; ++s) {
            std::strncpy(dst.long_term[s].name, src.long_term[s].name, 32);
            dst.long_term[s].occupied    = src.long_term[s].occupied;
            dst.long_term[s].slot_size   = src.long_term[s].slot_size;
            dst.long_term[s].is_artifact = src.long_term[s].is_artifact;
        }
    }

    sem_post(&state->global_mutex);
    return snap;
}

// ---------------------------------------------------------------------------
// Colour palette
// ---------------------------------------------------------------------------
namespace C {
    static const sf::Color PANEL_DARK  {10,  12,  22,  220};
    static const sf::Color PANEL_MID   {20,  24,  44,  230};
    static const sf::Color BORDER_GOLD {110, 80,  30,  255};
    static const sf::Color PLAYER_BG   {20,  40,  70,  200};
    static const sf::Color ENEMY_BG    {60,  20,  20,  200};
    static const sf::Color PLAYER_NAME {180, 220, 255, 255};
    static const sf::Color ENEMY_NAME  {255, 150, 90,  255};
    static const sf::Color DEAD_COL    {80,  80,  80,  160};
    static const sf::Color HP_HIGH     {200, 40,  40,  255};
    static const sf::Color HP_LOW      {255, 120, 0,   255};
    static const sf::Color HP_BG       {35,  10,  10,  200};
    static const sf::Color STAM_COL    {220, 185, 30,  255};
    static const sf::Color STAM_BG     {40,  35,  5,   200};
    static const sf::Color BTN_NORMAL  {30,  40,  65,  230};
    static const sf::Color BTN_HOVER   {60,  80,  130, 255};
    static const sf::Color BTN_DISABLE {25,  25,  25,  160};
    static const sf::Color BTN_TEXT    {210, 230, 255, 255};
    static const sf::Color STUN_COL    {160, 255, 90,  255};
    static const sf::Color WHITE       {255, 255, 255, 255};
    static const sf::Color MENU_BG     {12,  16,  30,  240};
}

// Card layout constants
static constexpr float kCardW   = 190.f;
static constexpr float kCardH   = 120.f;
static constexpr float kCardGap = 8.f;
static constexpr float kLeftX   = 20.f;
static constexpr float kRightX  = 660.f;
static constexpr float kStartY  = 100.f;

// Forward declarations for helpers used before definition.
static GuiButton makeButton(float x, float y, float w, float h,
                             const std::string& label,
                             bool enabled = true);
static void drawButton(sf::RenderWindow& win, sf::RectangleShape& rect,
                        sf::Text& text, sf::Font& font,
                        const GuiButton& btn);

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Renderer::Renderer(const std::string& assets_path,
                   const std::string& window_title)
    : m_sprites(assets_path + "/sprites/sprite.png")
{
    m_window.create(sf::VideoMode(1280, 720), window_title,
                    sf::Style::Titlebar | sf::Style::Close);
    m_window.setVerticalSyncEnabled(true);
    m_window.setFramerateLimit(60);
    m_win_w = 1280.f;
    m_win_h = 720.f;

    if (!m_bg_texture.loadFromFile(assets_path + "/backgrounds/dungeon.png"))
        std::fprintf(stderr, "Renderer: background not found\n");
    m_bg_texture.setSmooth(false);
    m_bg_sprite.setTexture(m_bg_texture);
    sf::Vector2u tsz = m_bg_texture.getSize();
    if (tsz.x > 0 && tsz.y > 0) {
        float sc = std::max(m_win_w / tsz.x, m_win_h / tsz.y);
        m_bg_sprite.setScale(sc, sc);
        m_bg_sprite.setPosition((m_win_w - tsz.x * sc) / 2.f,
                                 (m_win_h - tsz.y * sc) / 2.f);
    }

    if (!m_font.loadFromFile(assets_path + "/fonts/BlazeCircuitRegular.ttf"))
        std::fprintf(stderr, "Renderer: font not found\n");
    m_text.setFont(m_font);

    m_anims.fill(EntityAnim{});
    m_entity_pos.fill({0.f, 0.f});
    m_clock.restart();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool Renderer::isOpen() const { return m_window.isOpen(); }
void Renderer::close()        { m_window.close(); }

bool Renderer::pollEvents() {
    handleMouseMove(sf::Vector2f(sf::Mouse::getPosition(m_window)));
    sf::Event ev{};
    while (m_window.pollEvent(ev)) {
        if (ev.type == sf::Event::Closed) { m_window.close(); return false; }
        if (ev.type == sf::Event::MouseMoved)
            handleMouseMove({(float)ev.mouseMove.x, (float)ev.mouseMove.y});
        if (ev.type == sf::Event::MouseButtonReleased &&
            ev.mouseButton.button == sf::Mouse::Left)
            handleMouseClick({(float)ev.mouseButton.x,
                               (float)ev.mouseButton.y}, m_cached_snap);
    }
    return true;
}

bool Renderer::pollGuiAction(GuiAction& out) {
    if (!m_pending_gui_action.ready) return false;
    out = m_pending_gui_action;
    m_pending_gui_action = GuiAction{};
    return true;
}

void Renderer::setPlayerTurn(int player_idx, const RenderSnapshot& snap) {
    m_active_player = player_idx;
    m_cached_snap   = snap;
    if (player_idx >= 0) {
        m_phase = InputPhase::ACTION_MENU;
        buildActionButtons(snap.entities[player_idx]);
    } else {
        m_phase = InputPhase::NONE;
        m_action_buttons.clear();
        m_target_buttons.clear();
        m_inventory_buttons.clear();
        m_longterm_buttons.clear();
    }
}

// ---------------------------------------------------------------------------
// Animation triggers
// ---------------------------------------------------------------------------
void Renderer::triggerAttackAnim(int attacker_idx, int target_idx) {
    if (attacker_idx < 0 || attacker_idx >= MAX_ENTITIES) return;
    if (target_idx   < 0 || target_idx   >= MAX_ENTITIES) return;

    sf::Vector2f from = m_entity_pos[attacker_idx];
    sf::Vector2f to   = m_entity_pos[target_idx];
    float dx    = to.x - from.x;
    float slide = std::min(std::abs(dx) * 0.4f, 80.f);
    slide = (dx > 0.f) ? slide : -slide;

    auto& a = m_anims[attacker_idx];
    a.attacking     = true;
    a.attack_dx     = 0.f;
    a.attack_target = slide;
    a.attack_t      = 0.f;
    a.attack_return = false;
}

void Renderer::triggerWeaponFloat(int attacker_idx, int target_idx,
                                   const char* weapon_name)
{
    if (attacker_idx < 0 || attacker_idx >= MAX_ENTITIES) return;
    if (target_idx   < 0 || target_idx   >= MAX_ENTITIES) return;
    if (!weapon_name || weapon_name[0] == '\0') return;

    auto& a = m_anims[attacker_idx];
    a.weapon_float        = true;
    a.weapon_float_target = target_idx;
    a.weapon_float_t      = 0.f;
    std::strncpy(a.weapon_float_name, weapon_name,
                 sizeof(a.weapon_float_name) - 1);
    a.weapon_float_name[sizeof(a.weapon_float_name) - 1] = '\0';
}

void Renderer::triggerHitFlash(int target_idx) {
    if (target_idx < 0 || target_idx >= MAX_ENTITIES) return;
    m_anims[target_idx].hit_flash   = true;
    m_anims[target_idx].hit_flash_t = 1.f;
}

void Renderer::triggerStunFlash(int target_idx) {
    if (target_idx < 0 || target_idx >= MAX_ENTITIES) return;
    auto& a = m_anims[target_idx];
    a.stun_flash   = true;
    a.stun_flash_t = 0.f;
    a.stun_flashes = 3;   // 3 visible flashes
}

void Renderer::triggerDeath(int entity_idx) {
    if (entity_idx < 0 || entity_idx >= MAX_ENTITIES) return;
    // dead_confirmed is set in render() when is_alive flips to false
    // nothing extra needed here; kept for API completeness
}

// ---------------------------------------------------------------------------
// stepAnimations
// ---------------------------------------------------------------------------
void Renderer::stepAnimations(float dt) {
    const float ATTACK_SPD  = 4.f;   // full slide in ~0.25 s
    const float FLASH_SPD   = 3.5f;
    const float STUN_SPD    = 6.f;   // each flash cycle ~0.17 s → 3 flashes ~0.5 s
    const float PULSE_SPD   = 2.2f;
    const float DEATH_SPD   = 80.f;  // alpha units per second

    for (int i = 0; i < MAX_ENTITIES; ++i) {
        auto& a = m_anims[i];

        // ---- attack slide ------------------------------------------------
        if (a.attacking) {
            a.attack_t += dt * ATTACK_SPD;
            if (!a.attack_return) {
                float t    = std::min(a.attack_t, 1.f);
                a.attack_dx = a.attack_target * t;
                if (a.attack_t >= 1.f) {
                    a.attack_return = true;
                    a.attack_t      = 0.f;
                }
            } else {
                float t    = std::min(a.attack_t, 1.f);
                a.attack_dx = a.attack_target * (1.f - t);
                if (a.attack_t >= 1.f) {
                    a.attacking     = false;
                    a.attack_dx     = 0.f;
                    a.attack_return = false;
                    a.attack_t      = 0.f;
                }
            }

            // ---- weapon float follows the forward phase ------------------
            if (a.weapon_float) {
                if (!a.attack_return) {
                    // advance float from 0 to 1 while going forward
                    a.weapon_float_t = std::min(a.attack_t, 1.f);
                } else {
                    // hide on return
                    a.weapon_float   = false;
                    a.weapon_float_t = 0.f;
                }
            }
        } else {
            // If attack ended but float somehow still on, clear it
            if (a.weapon_float) {
                a.weapon_float   = false;
                a.weapon_float_t = 0.f;
            }
        }

        // ---- hit flash ---------------------------------------------------
        if (a.hit_flash) {
            a.hit_flash_t -= dt * FLASH_SPD;
            if (a.hit_flash_t <= 0.f) {
                a.hit_flash_t = 0.f;
                a.hit_flash   = false;
            }
        }

        // ---- stun flash burst --------------------------------------------
        if (a.stun_flash && a.stun_flashes > 0) {
            a.stun_flash_t += dt * STUN_SPD;
            if (a.stun_flash_t >= 1.f) {
                a.stun_flash_t = 0.f;
                a.stun_flashes--;
                if (a.stun_flashes <= 0) {
                    a.stun_flash   = false;
                    a.stun_flash_t = 0.f;
                }
            }
        }

        // ---- death fade --------------------------------------------------
        if (a.dead_confirmed && a.death_alpha > 0.f) {
            a.death_alpha -= dt * DEATH_SPD;
            if (a.death_alpha < 0.f) a.death_alpha = 0.f;
        }

        // ---- turn pulse --------------------------------------------------
        a.pulse_t += dt * PULSE_SPD;
    }
}

// ---------------------------------------------------------------------------
// render  – main per-frame entry point
// ---------------------------------------------------------------------------
void Renderer::render(const RenderSnapshot& snap) {
    m_cached_snap = snap;

    float dt = m_clock.restart().asSeconds();
    // Clamp dt to avoid huge jumps if the window was minimised etc.
    if (dt > 0.1f) dt = 0.1f;
    stepAnimations(dt);

    // Mark newly-dead entities so their fade starts
    int total = snap.player_count + snap.npc_count;
    for (int i = 0; i < total; ++i) {
        if (!snap.entities[i].is_alive && !m_anims[i].dead_confirmed) {
            m_anims[i].dead_confirmed = true;
            m_anims[i].death_alpha    = 255.f;
        }
        if (!snap.entities[i].is_my_turn)
            m_anims[i].pulse_t = 0.f;
    }

    m_window.clear(sf::Color(8, 10, 20));
    drawBackground();
    drawBattlefield(snap);

    // Floating weapon icons drawn on top of all cards
    drawFloatingWeapons();

    if (m_phase == InputPhase::ACTION_MENU)          drawActionMenu(snap);
    else if (m_phase == InputPhase::TARGET_SELECT)   drawTargetOverlay(snap);
    else if (m_phase == InputPhase::INVENTORY_SELECT) drawInventoryMenu(snap);
    else if (m_phase == InputPhase::LONGTERM_SELECT)  drawLongTermMenu(snap);
    else if (m_phase == InputPhase::DROP_OFFER)       drawDropOfferDialog();

    if (snap.ultimate_active) drawUltimateOverlay();
    drawStatusBar(snap);
    m_window.display();
}

// ---------------------------------------------------------------------------
// drawBackground
// ---------------------------------------------------------------------------
void Renderer::drawBackground() {
    m_bg_sprite.setColor(sf::Color(255, 255, 255, 130));
    m_window.draw(m_bg_sprite);
}

void Renderer::drawText(const std::string& str, float x, float y,
                         unsigned int size, sf::Color col)
{
    m_text.setFont(m_font);
    m_text.setString(str);
    m_text.setCharacterSize(size);
    m_text.setFillColor(col);
    m_text.setPosition(x, y);
    m_window.draw(m_text);
}

void Renderer::drawPanelBackground(float x, float y, float w, float h,
                                    sf::Color fill)
{
    m_rect.setSize({w, h});
    m_rect.setPosition(x, y);
    m_rect.setFillColor(fill);
    m_rect.setOutlineThickness(1.5f);
    m_rect.setOutlineColor(C::BORDER_GOLD);
    m_window.draw(m_rect);
}

// ---------------------------------------------------------------------------
// drawBattlefield
// Players left column (x≈20), enemies right column (x≈660)
// ---------------------------------------------------------------------------
void Renderer::drawBattlefield(const RenderSnapshot& snap) {
    drawText("CHRONO  RIFT", 12.f, 8.f, 28u, C::BORDER_GOLD);

    // Centre divider
    sf::RectangleShape div({2.f, m_win_h - 200.f});
    div.setPosition(m_win_w / 2.f, 45.f);
    div.setFillColor(sf::Color(C::BORDER_GOLD.r, C::BORDER_GOLD.g,
                                C::BORDER_GOLD.b, 80));
    m_window.draw(div);

    drawText("PLAYERS", 80.f,  42.f, kFontMd, C::PLAYER_NAME);
    drawText("ENEMIES", 830.f, 42.f, kFontMd, C::ENEMY_NAME);

    int total = snap.player_count + snap.npc_count;

    int player_ids[MAX_ENTITIES] = {};
    int enemy_ids[MAX_ENTITIES]  = {};
    int p_count = 0;
    int e_count = 0;

    for (int i = 0; i < total && i < MAX_ENTITIES; ++i) {
        if (snap.entities[i].is_player)
            player_ids[p_count++] = i;
        else
            enemy_ids[e_count++] = i;
    }

    int p_cols = (p_count > 3) ? 2 : 1;
    int e_cols = (e_count > 5) ? 2 : 1;

    for (int idx = 0; idx < p_count; ++idx) {
        int row = idx / p_cols;
        int col = idx % p_cols;
        float cx = kLeftX + col * (kCardW + kCardGap);
        float cy = kStartY + row * (kCardH + kCardGap);
        int ent_idx = player_ids[idx];
        const auto& ent = snap.entities[ent_idx];

        // Store card centre for animation position interpolation
        m_entity_pos[ent_idx] = {cx + kCardW / 2.f, cy + kCardH / 2.f};

        float draw_x = cx + m_anims[ent_idx].attack_dx;
        drawEntityCard(ent, ent_idx, {draw_x, cy},
                       snap.current_turn_idx == ent_idx);
    }

    for (int idx = 0; idx < e_count; ++idx) {
        int row = idx / e_cols;
        int col = idx % e_cols;
        float cx = kRightX + col * (kCardW + kCardGap);
        float cy = kStartY + row * (kCardH + kCardGap);
        int ent_idx = enemy_ids[idx];
        const auto& ent = snap.entities[ent_idx];

        // Store card centre for animation position interpolation
        m_entity_pos[ent_idx] = {cx + kCardW / 2.f, cy + kCardH / 2.f};

        float draw_x = cx + m_anims[ent_idx].attack_dx;
        drawEntityCard(ent, ent_idx, {draw_x, cy},
                       snap.current_turn_idx == ent_idx);
    }
}

// ---------------------------------------------------------------------------
// drawEntityCard
// ---------------------------------------------------------------------------
void Renderer::drawEntityCard(const RenderSnapshot::EntitySnap& ent,
                               int entity_index,
                               sf::Vector2f pos,
                               bool highlight)
{
    const float W = kCardW;
    const float H = kCardH;
    auto& anim = m_anims[entity_index];

    // Card bg
    sf::Color bg = ent.is_player ? C::PLAYER_BG : C::ENEMY_BG;
    if (!ent.is_alive) bg = sf::Color(20, 20, 20, 160);
    drawPanelBackground(pos.x, pos.y, W, H, bg);

    // Turn highlight pulsing border
    if (highlight && ent.is_alive) {
        float pulse = 0.5f + 0.5f * std::sin(anim.pulse_t);
        m_rect.setSize({W, H});
        m_rect.setPosition(pos.x, pos.y);
        m_rect.setFillColor(sf::Color(255, 230, 100,
                                       (sf::Uint8)(20.f * pulse)));
        m_rect.setOutlineThickness(2.5f);
        m_rect.setOutlineColor(sf::Color(255, 230, 100,
                                          (sf::Uint8)(80.f + 120.f * pulse)));
        m_window.draw(m_rect);
    }

    // Sprite with turn-pulse scale
    float scale_bump  = (highlight && ent.is_alive)
                        ? 1.f + 0.04f * std::sin(anim.pulse_t) : 1.f;
    float sprite_base = 72.f;
    float sprite_size = sprite_base * scale_bump;
    float sprite_x    = pos.x + (W - sprite_size) / 2.f;
    float sprite_y    = pos.y + 4.f;

    m_sprites.drawEntity(m_window, entity_index, ent.is_player,
                          {sprite_x, sprite_y},
                          {sprite_size, sprite_size});

    // Hit flash overlay
    if (anim.hit_flash && anim.hit_flash_t > 0.f) {
        m_rect.setSize({sprite_size, sprite_size});
        m_rect.setPosition(sprite_x, sprite_y);
        m_rect.setFillColor(sf::Color(255, 30, 30,
                                       (sf::Uint8)(150.f * anim.hit_flash_t)));
        m_rect.setOutlineThickness(0.f);
        m_window.draw(m_rect);
    }

    // Stun flash burst  – draw the stunned icon flashing over the card
    if (anim.stun_flash && anim.stun_flashes > 0) {
        // Blink: visible in first half of each cycle
        bool visible = anim.stun_flash_t < 0.5f;
        if (visible) {
            // Large stunned icon centred on the card
            float icon_sz = 48.f;
            float icon_x  = pos.x + (W - icon_sz) / 2.f;
            float icon_y  = pos.y + (H - icon_sz) / 2.f - 10.f;
            m_sprites.drawStatusIcon(m_window, "stunned",
                                      {icon_x, icon_y},
                                      {icon_sz, icon_sz});
            // Translucent yellow overlay so it really pops
            m_rect.setSize({W, H});
            m_rect.setPosition(pos.x, pos.y);
            m_rect.setFillColor(sf::Color(200, 255, 80, 40));
            m_rect.setOutlineThickness(0.f);
            m_window.draw(m_rect);
        }
    }

    // Death fade
    if (anim.dead_confirmed && anim.death_alpha < 255.f) {
        float fade = 1.f - anim.death_alpha / 255.f;
        m_rect.setSize({W, H});
        m_rect.setPosition(pos.x, pos.y);
        m_rect.setFillColor(sf::Color(0, 0, 0,
                                       (sf::Uint8)(200.f * fade)));
        m_rect.setOutlineThickness(0.f);
        m_window.draw(m_rect);
    }

    // Name
    sf::Color name_col = ent.is_player ? C::PLAYER_NAME : C::ENEMY_NAME;
    if (!ent.is_alive) name_col = C::DEAD_COL;
    drawText(ent.name, pos.x + 4.f, pos.y + 70.f, kFontSm, name_col);

    if (!ent.is_alive) {
        drawText("DEAD", pos.x + W / 2.f - 16.f, pos.y + H / 2.f - 8.f,
                 kFontMd, sf::Color(180, 30, 30, 200));
        return;
    }

    // HP bar
    drawHpBar(pos.x + 4.f, pos.y + 84.f, W - 8.f, 8.f, ent.hp, ent.max_hp);
    char hpbuf[32];
    std::snprintf(hpbuf, sizeof(hpbuf), "%d/%d", ent.hp, ent.max_hp);
    drawText(hpbuf, pos.x + 4.f, pos.y + 93.f, kFontSm - 1,
             sf::Color(200, 200, 200, 200));

    // Stamina bar
    drawStaminaBar(pos.x + 4.f, pos.y + 104.f, W - 8.f, 6.f,
                   ent.stamina, ent.max_stamina);
    int sp = ent.max_stamina > 0.f
             ? (int)(ent.stamina / ent.max_stamina * 100.f) : 0;
    char sbuf[16];
    std::snprintf(sbuf, sizeof(sbuf), "SP %d%%", sp);
    drawText(sbuf, pos.x + W - 42.f, pos.y + 103.f, kFontSm - 2, C::STAM_COL);

    // Status icons (turn arrow + stunned label)
    float icon_x = pos.x + 4.f;
    if (highlight) {
        m_sprites.drawStatusIcon(m_window, "turn",
                                  {icon_x, pos.y + 70.f}, {14.f, 14.f});
        icon_x += 18.f;
    }
    if (ent.is_stunned) {
        m_sprites.drawStatusIcon(m_window, "stunned",
                                  {icon_x, pos.y + 70.f}, {14.f, 14.f});
        drawText("STUN", icon_x + 16.f, pos.y + 70.f,
                 kFontSm - 2, C::STUN_COL);
    }

    // Weapon icon strip (up to 3)
    int drawn = 0;
    for (int s = 0; s < INVENTORY_SLOTS && drawn < 3; ++s) {
        const auto& slot = ent.inventory[s];
        if (!slot.occupied || slot.name[0] == '\0' || slot.slot_size <= 0)
            continue;
        m_sprites.drawWeaponIcon(m_window, slot.name,
                                  {pos.x + 4.f + drawn * 22.f, pos.y + 50.f},
                                  {18.f, 18.f});
        ++drawn;
    }
}

// ---------------------------------------------------------------------------
// showDropOffer  – called from renderThreadSFML when a drop arrives
// ---------------------------------------------------------------------------
void Renderer::showDropOffer(int player_idx, const char* weapon_name) {
    m_drop_for_player = player_idx;
    std::strncpy(m_drop_weapon_name, weapon_name,
                 sizeof(m_drop_weapon_name) - 1);
    m_drop_weapon_name[sizeof(m_drop_weapon_name) - 1] = '\0';
    m_phase = InputPhase::DROP_OFFER;
}

// ---------------------------------------------------------------------------
// drawDropOfferDialog
// ---------------------------------------------------------------------------
void Renderer::drawDropOfferDialog() {
    // Dark overlay
    m_rect.setSize({m_win_w, m_win_h});
    m_rect.setPosition(0.f, 0.f);
    m_rect.setFillColor(sf::Color(0, 0, 0, 160));
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);

    const float DW = 340.f, DH = 160.f;
    const float DX = (m_win_w - DW) / 2.f;
    const float DY = (m_win_h - DH) / 2.f;

    drawPanelBackground(DX, DY, DW, DH, C::MENU_BG);

    // Weapon icon centred at top of dialog
    m_sprites.drawWeaponIcon(m_window, m_drop_weapon_name,
                              {DX + DW / 2.f - 20.f, DY + 10.f},
                              {40.f, 40.f});

    // Title
    char title[64];
    std::snprintf(title, sizeof(title), "%s dropped!", m_drop_weapon_name);
    drawText(title, DX + 10.f, DY + 54.f, kFontMd, C::BORDER_GOLD);

    char sub[64];
    std::snprintf(sub, sizeof(sub), "Player %d: Pick it up?",
                  m_drop_for_player + 1);
    drawText(sub, DX + 10.f, DY + 76.f, kFontSm, C::WHITE);

    // YES button
    GuiButton yes_btn = makeButton(DX + 20.f, DY + 108.f, 130.f, 36.f,
                                   "YES  Pick up");
    yes_btn.hovered = sf::FloatRect{DX+20.f, DY+108.f, 130.f, 36.f}
                      .contains(sf::Vector2f(sf::Mouse::getPosition(m_window)));
    drawButton(m_window, m_rect, m_text, m_font, yes_btn);

    // NO button
    GuiButton no_btn  = makeButton(DX + 180.f, DY + 108.f, 130.f, 36.f,
                                   "NO  Leave it");
    no_btn.hovered  = sf::FloatRect{DX+180.f, DY+108.f, 130.f, 36.f}
                      .contains(sf::Vector2f(sf::Mouse::getPosition(m_window)));
    drawButton(m_window, m_rect, m_text, m_font, no_btn);
}
// Drawn after all cards so it appears on top.
// Interpolates the icon position between attacker card centre and the midpoint
// between attacker and target.
// ---------------------------------------------------------------------------
void Renderer::drawFloatingWeapons() {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        auto& a = m_anims[i];
        if (!a.weapon_float || a.weapon_float_name[0] == '\0') continue;
        if (a.weapon_float_target < 0 ||
            a.weapon_float_target >= MAX_ENTITIES)   continue;

        sf::Vector2f from  = m_entity_pos[i];
        sf::Vector2f to    = m_entity_pos[a.weapon_float_target];
        // Travel to midpoint only
        sf::Vector2f mid   = {(from.x + to.x) / 2.f,
                               (from.y + to.y) / 2.f - 20.f};

        float t = a.weapon_float_t;   // 0 → 1
        sf::Vector2f cur = {from.x + (mid.x - from.x) * t,
                             from.y + (mid.y - from.y) * t};

        // Size pulses slightly as it flies
        float sz = 32.f + 8.f * std::sin(t * 3.14159f);

        // Draw a glow halo behind the icon
        m_rect.setSize({sz + 10.f, sz + 10.f});
        m_rect.setPosition(cur.x - sz / 2.f - 5.f, cur.y - sz / 2.f - 5.f);
        m_rect.setFillColor(sf::Color(255, 220, 60, (sf::Uint8)(80.f * t)));
        m_rect.setOutlineThickness(0.f);
        m_window.draw(m_rect);

        // Draw the weapon icon centred at cur
        m_sprites.drawWeaponIcon(m_window, a.weapon_float_name,
                                  {cur.x - sz / 2.f, cur.y - sz / 2.f},
                                  {sz, sz});
    }
}

// ---------------------------------------------------------------------------
// drawHpBar / drawStaminaBar
// ---------------------------------------------------------------------------
void Renderer::drawHpBar(float x, float y, float w, float h,
                          int hp, int max_hp)
{
    m_rect.setSize({w, h});
    m_rect.setPosition(x, y);
    m_rect.setFillColor(C::HP_BG);
    m_rect.setOutlineThickness(1.f);
    m_rect.setOutlineColor(sf::Color(80, 20, 20, 180));
    m_window.draw(m_rect);
    float r = max_hp > 0
        ? std::max(0.f, std::min(1.f, (float)hp / max_hp)) : 0.f;
    m_rect.setSize({w * r, h});
    m_rect.setFillColor(r < 0.3f ? C::HP_LOW : C::HP_HIGH);
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);
}

void Renderer::drawStaminaBar(float x, float y, float w, float h,
                               float stamina, float max_stamina)
{
    m_rect.setSize({w, h});
    m_rect.setPosition(x, y);
    m_rect.setFillColor(C::STAM_BG);
    m_rect.setOutlineThickness(1.f);
    m_rect.setOutlineColor(sf::Color(60, 50, 10, 160));
    m_window.draw(m_rect);
    float r = max_stamina > 0.f
        ? std::max(0.f, std::min(1.f, stamina / max_stamina)) : 0.f;
    m_rect.setSize({w * r, h});
    m_rect.setFillColor(C::STAM_COL);
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);
}

// ---------------------------------------------------------------------------
// drawStatusBar
// ---------------------------------------------------------------------------
void Renderer::drawStatusBar(const RenderSnapshot& snap) {
    float sy = m_win_h - 38.f;
    drawPanelBackground(0.f, sy, m_win_w, 38.f, C::PANEL_DARK);
    int idx = snap.current_turn_idx;
    if (idx >= 0 && idx < snap.player_count + snap.npc_count) {
        const auto& e = snap.entities[idx];
        std::string msg;
        if (e.is_my_turn) {
            msg = std::string(e.name) + "'s turn";
            if (e.is_player && m_phase != InputPhase::NONE)
                msg += "  —  Choose an action";
            else if (!e.is_player)
                msg += "  —  Enemy is thinking...";
        }
        drawText(msg, 12.f, sy + 10.f, kFontMd, C::WHITE);
    }
    if (snap.ultimate_active)
        drawText("ULTIMATE ACTIVE", m_win_w - 200.f, sy + 10.f,
                 kFontMd, sf::Color(255, 220, 60, 255));
}

// ---------------------------------------------------------------------------
// Button helpers
// ---------------------------------------------------------------------------
static GuiButton makeButton(float x, float y, float w, float h,
                             const std::string& label, bool enabled)
{
    GuiButton b;
    b.bounds  = {x, y, w, h};
    b.label   = label;
    b.enabled = enabled;
    return b;
}

static void drawButton(sf::RenderWindow& win, sf::RectangleShape& rect,
                        sf::Text& text, sf::Font& font,
                        const GuiButton& btn)
{
    sf::Color fill = btn.enabled
        ? (btn.hovered ? C::BTN_HOVER : C::BTN_NORMAL) : C::BTN_DISABLE;
    rect.setSize({btn.bounds.width, btn.bounds.height});
    rect.setPosition(btn.bounds.left, btn.bounds.top);
    rect.setFillColor(fill);
    rect.setOutlineThickness(1.5f);
    rect.setOutlineColor(btn.enabled ? C::BORDER_GOLD
                                     : sf::Color(50, 50, 50, 120));
    win.draw(rect);
    text.setFont(font);
    text.setString(btn.label);
    text.setCharacterSize(13u);
    text.setFillColor(btn.enabled ? C::BTN_TEXT : C::DEAD_COL);
    sf::FloatRect tb = text.getLocalBounds();
    text.setPosition(
        btn.bounds.left + (btn.bounds.width  - tb.width)  / 2.f,
        btn.bounds.top  + (btn.bounds.height - tb.height) / 2.f - 2.f);
    win.draw(text);
}

void Renderer::buildActionButtons(const RenderSnapshot::EntitySnap& actor) {
    m_action_buttons.clear();
    const float BW = 180.f, BH = 32.f, GAP = 5.f;
    const float MX = (m_win_w - BW * 2.f - GAP) / 2.f;
    const float MY = 480.f;

    struct { const char* label; bool enabled; } acts[] = {
        {"Strike",    true},
        {"Exhaust",   true},
        {"Heal",      true},
        {"Skip",      true},
        {"Use Weapon",true},
        {"Swap In",   actor.long_term_count > 0},
        {"Ultimate",  actor.holds_solar_core && actor.holds_lunar_blade},
        {"Stun",      true},
        {"Quit",      true},
    };
    for (int i = 0; i < 9; ++i) {
        int col = i % 2, row = i / 2;
        m_action_buttons.push_back(
            makeButton(MX + col * (BW + GAP), MY + row * (BH + GAP),
                       BW, BH, acts[i].label, acts[i].enabled));
    }
}

void Renderer::buildTargetButtons(const RenderSnapshot& snap) {
    m_target_buttons.clear();
    const float BW = 200.f, BH = 34.f, GAP = 6.f;
    const float MX = m_win_w / 2.f - BW / 2.f;
    float by = 120.f;
    int total = snap.player_count + snap.npc_count;
    for (int i = 0; i < total; ++i) {
        const auto& e = snap.entities[i];
        if (e.is_player || !e.is_alive) continue;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[%d] %s  HP:%d", i, e.name, e.hp);
        auto btn = makeButton(MX, by, BW, BH, buf, true);
        btn.bounds.left = (float)i;   // encode entity index
        m_target_buttons.push_back(btn);
        by += BH + GAP;
    }
}

void Renderer::buildInventoryButtons(const RenderSnapshot::EntitySnap& actor) {
    m_inventory_buttons.clear();
    const float BW = 230.f, BH = 34.f, GAP = 6.f;
    const float MX = m_win_w / 2.f - BW / 2.f;
    float by = 100.f;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const auto& slot = actor.inventory[i];
        if (!slot.occupied || slot.name[0] == '\0' || slot.slot_size <= 0)
            continue;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[%d] %s (slots:%d)", i,
                      slot.name, slot.slot_size);
        auto btn = makeButton(MX, by, BW, BH, buf, true);
        btn.bounds.left = (float)i;
        m_inventory_buttons.push_back(btn);
        by += BH + GAP;
    }
    if (m_inventory_buttons.empty())
        m_inventory_buttons.push_back(
            makeButton(m_win_w/2.f-115.f, 100.f, 230.f, 34.f,
                       "No weapons", false));
}

void Renderer::buildLongTermButtons(const RenderSnapshot::EntitySnap& actor) {
    m_longterm_buttons.clear();
    const float BW = 230.f, BH = 34.f, GAP = 6.f;
    const float MX = m_win_w / 2.f - BW / 2.f;
    float by = 100.f;
    for (int i = 0; i < actor.long_term_count && i < LONG_TERM_SIZE; ++i) {
        const auto& slot = actor.long_term[i];
        if (slot.name[0] == '\0') continue;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[%d] %s", i, slot.name);
        auto btn = makeButton(MX, by, BW, BH, buf, true);
        btn.bounds.left = (float)i;
        m_longterm_buttons.push_back(btn);
        by += BH + GAP;
    }
    if (m_longterm_buttons.empty())
        m_longterm_buttons.push_back(
            makeButton(m_win_w/2.f-115.f, 100.f, 230.f, 34.f,
                       "Long-term empty", false));
}

// ---------------------------------------------------------------------------
// Draw menus
// ---------------------------------------------------------------------------
void Renderer::drawActionMenu(const RenderSnapshot& snap) {
    if (m_active_player < 0) return;
    float panel_y = 540.f;
    drawPanelBackground(0.f, panel_y, m_win_w, m_win_h - panel_y, C::MENU_BG);
    drawText("Choose Action:", 12.f, panel_y + 6.f, kFontMd, C::BORDER_GOLD);
    for (auto& btn : m_action_buttons)
        drawButton(m_window, m_rect, m_text, m_font, btn);
}

void Renderer::drawTargetOverlay(const RenderSnapshot& snap) {
    m_rect.setSize({m_win_w, m_win_h});
    m_rect.setPosition(0.f, 0.f);
    m_rect.setFillColor(sf::Color(0, 0, 0, 140));
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);

    const float BW = 200.f, MX = m_win_w/2.f - BW/2.f;
    drawPanelBackground(MX - 10.f, 60.f, BW + 20.f,
                         50.f + m_target_buttons.size() * 42.f, C::MENU_BG);
    drawText("Select Target:", MX, 72.f, kFontMd, C::BORDER_GOLD);

    float by = 100.f;
    for (auto& btn : m_target_buttons) {
        GuiButton db = btn; db.bounds.left = MX; db.bounds.top = by;
        drawButton(m_window, m_rect, m_text, m_font, db);
        by += 40.f;
    }
    drawButton(m_window, m_rect, m_text, m_font,
               makeButton(MX, by + 6.f, BW, 32.f, "< Back"));
}

void Renderer::drawInventoryMenu(const RenderSnapshot& snap) {
    m_rect.setSize({m_win_w, m_win_h});
    m_rect.setPosition(0.f, 0.f);
    m_rect.setFillColor(sf::Color(0, 0, 0, 140));
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);

    const float BW = 230.f, MX = m_win_w/2.f - BW/2.f;
    drawPanelBackground(MX - 10.f, 60.f, BW + 20.f,
                         50.f + (m_inventory_buttons.size()+1)*42.f, C::MENU_BG);
    drawText("Select Weapon:", MX, 72.f, kFontMd, C::BORDER_GOLD);

    float by = 100.f;
    for (auto& btn : m_inventory_buttons) {
        GuiButton db = btn; db.bounds.left = MX; db.bounds.top = by;
        drawButton(m_window, m_rect, m_text, m_font, db);
        by += 40.f;
    }
    drawButton(m_window, m_rect, m_text, m_font,
               makeButton(MX, by + 6.f, BW, 32.f, "< Back"));
}

void Renderer::drawLongTermMenu(const RenderSnapshot& snap) {
    m_rect.setSize({m_win_w, m_win_h});
    m_rect.setPosition(0.f, 0.f);
    m_rect.setFillColor(sf::Color(0, 0, 0, 140));
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);

    const float BW = 230.f, MX = m_win_w/2.f - BW/2.f;
    drawPanelBackground(MX - 10.f, 60.f, BW + 20.f,
                         50.f + (m_longterm_buttons.size()+1)*42.f, C::MENU_BG);
    drawText("Swap In (Long-Term):", MX, 72.f, kFontMd, C::BORDER_GOLD);

    float by = 100.f;
    for (auto& btn : m_longterm_buttons) {
        GuiButton db = btn; db.bounds.left = MX; db.bounds.top = by;
        drawButton(m_window, m_rect, m_text, m_font, db);
        by += 40.f;
    }
    drawButton(m_window, m_rect, m_text, m_font,
               makeButton(MX, by + 6.f, BW, 32.f, "< Back"));
}

// ---------------------------------------------------------------------------
// drawUltimateOverlay
// ---------------------------------------------------------------------------
void Renderer::drawUltimateOverlay() {
    static float pt = 0.f; pt += 0.04f;
    sf::Uint8 ba = (sf::Uint8)(90.f + 80.f * std::abs(std::sin(pt)));
    sf::RectangleShape ov({m_win_w, m_win_h});
    ov.setPosition(0.f, 0.f);
    ov.setFillColor(sf::Color(255, 180, 20, 25));
    m_window.draw(ov);
    float bw = 5.f;
    sf::Color bc(255, 200, 50, ba);
    auto edge = [&](float x, float y, float w, float h) {
        sf::RectangleShape r({w,h}); r.setPosition(x,y);
        r.setFillColor(bc); m_window.draw(r);
    };
    edge(0,0,m_win_w,bw); edge(0,m_win_h-bw,m_win_w,bw);
    edge(0,0,bw,m_win_h); edge(m_win_w-bw,0,bw,m_win_h);
    m_text.setCharacterSize(24u);
    m_text.setFillColor(sf::Color(255,230,80,ba));
    m_text.setString("ULTIMATE ACTIVE");
    sf::FloatRect tb = m_text.getLocalBounds();
    m_text.setPosition((m_win_w - tb.width)/2.f, 10.f);
    m_window.draw(m_text);
    m_sprites.drawStatusIcon(m_window, "ultimate",
                              {(m_win_w-40.f)/2.f-60.f, 8.f}, {28.f,28.f});
}

// ---------------------------------------------------------------------------
// Mouse handling
// ---------------------------------------------------------------------------
void Renderer::handleMouseMove(sf::Vector2f mp) {
    auto hover = [&](std::vector<GuiButton>& btns) {
        for (auto& b : btns) b.hovered = b.bounds.contains(mp);
    };
    hover(m_action_buttons);
    hover(m_target_buttons);
    hover(m_inventory_buttons);
    hover(m_longterm_buttons);
}

void Renderer::handleMouseClick(sf::Vector2f mp, const RenderSnapshot& snap) {
    if (m_pending_gui_action.ready) return;

    // ---- ACTION MENU -------------------------------------------------------
    if (m_phase == InputPhase::ACTION_MENU) {
        // Order must match buildActionButtons() exactly:
        // Strike, Exhaust, Heal, Skip, Use Weapon, Swap In, Ultimate, Stun, Quit
        ActionType action_types[] = {
            ActionType::STRIKE,     // button 0
            ActionType::EXHAUST,    // button 1
            ActionType::HEAL,       // button 2
            ActionType::SKIP,       // button 3
            ActionType::USE_WEAPON, // button 4
            ActionType::SWAP_IN,    // button 5
            ActionType::ULTIMATE,   // button 6
            ActionType::STUN,       // button 7
            ActionType::QUIT        // button 8
        };
        for (int i = 0; i < (int)m_action_buttons.size(); ++i) {
            const auto& btn = m_action_buttons[i];
            if (!btn.enabled || !btn.bounds.contains(mp)) continue;
            ActionType chosen = action_types[i];

            if (chosen == ActionType::HEAL  || chosen == ActionType::SKIP ||
                chosen == ActionType::QUIT  || chosen == ActionType::ULTIMATE)
            {
                m_pending_gui_action = {true, chosen, -1, -1};
                m_phase = InputPhase::NONE;
                return;
            }
            if (chosen == ActionType::STRIKE || chosen == ActionType::EXHAUST ||
                chosen == ActionType::STUN)
            {
                m_pending_action = chosen;
                m_phase = InputPhase::TARGET_SELECT;
                buildTargetButtons(snap);
                return;
            }
            if (chosen == ActionType::USE_WEAPON) {
                m_pending_action = ActionType::USE_WEAPON;
                m_phase = InputPhase::INVENTORY_SELECT;
                buildInventoryButtons(snap.entities[m_active_player]);
                return;
            }
            if (chosen == ActionType::SWAP_IN) {
                m_pending_action = ActionType::SWAP_IN;
                m_phase = InputPhase::LONGTERM_SELECT;
                buildLongTermButtons(snap.entities[m_active_player]);
                return;
            }
        }
        return;
    }

    // ---- TARGET SELECT -----------------------------------------------------
    if (m_phase == InputPhase::TARGET_SELECT) {
        const float BW = 200.f, MX = m_win_w/2.f - BW/2.f;
        float by = 100.f;
        for (auto& btn : m_target_buttons) {
            sf::FloatRect dr{MX, by, BW, 34.f};
            if (dr.contains(mp)) {
                int entity_idx = (int)btn.bounds.left;
                // Preserve weapon_slot that was set in INVENTORY_SELECT phase
                int saved_slot = m_pending_gui_action.weapon_slot;
                m_pending_gui_action.ready       = true;
                m_pending_gui_action.action      = m_pending_action;
                m_pending_gui_action.target_idx  = entity_idx;
                m_pending_gui_action.weapon_slot = saved_slot;
                m_phase = InputPhase::NONE;
                return;
            }
            by += 40.f;
        }
        // Back
        sf::FloatRect back{MX, by + 6.f, BW, 32.f};
        if (back.contains(mp)) {
            // If we came from inventory, go back there, else action menu
            if (m_pending_action == ActionType::USE_WEAPON &&
                m_pending_gui_action.weapon_slot >= 0)
            {
                m_phase = InputPhase::INVENTORY_SELECT;
                buildInventoryButtons(snap.entities[m_active_player]);
            } else {
                m_phase = InputPhase::ACTION_MENU;
                buildActionButtons(snap.entities[m_active_player]);
                m_pending_gui_action.weapon_slot = -1;
            }
        }
        return;
    }

    // ---- INVENTORY SELECT --------------------------------------------------
    if (m_phase == InputPhase::INVENTORY_SELECT) {
        const float BW = 230.f, MX = m_win_w/2.f - BW/2.f;
        float by = 100.f;
        for (auto& btn : m_inventory_buttons) {
            sf::FloatRect dr{MX, by, BW, 34.f};
            if (btn.enabled && dr.contains(mp)) {
                m_pending_gui_action.weapon_slot = (int)btn.bounds.left;
                m_pending_action = ActionType::USE_WEAPON;
                m_phase = InputPhase::TARGET_SELECT;
                buildTargetButtons(snap);
                return;
            }
            by += 40.f;
        }
        sf::FloatRect back{MX, by + 6.f, BW, 32.f};
        if (back.contains(mp)) {
            m_phase = InputPhase::ACTION_MENU;
            buildActionButtons(snap.entities[m_active_player]);
            m_pending_gui_action.weapon_slot = -1;
        }
        return;
    }

    // ---- DROP OFFER --------------------------------------------------------
    if (m_phase == InputPhase::DROP_OFFER) {
        const float DW = 340.f, DH = 160.f;
        const float DX = (m_win_w - DW) / 2.f;
        const float DY = (m_win_h - DH) / 2.f;

        sf::FloatRect yes_r{DX + 20.f,  DY + 108.f, 130.f, 36.f};
        sf::FloatRect no_r {DX + 180.f, DY + 108.f, 130.f, 36.f};

        if (yes_r.contains(mp)) {
            // Player accepts the drop → USE_WEAPON with sentinel -2
            m_pending_gui_action.ready       = true;
            m_pending_gui_action.action      = ActionType::USE_WEAPON;
            m_pending_gui_action.target_idx  = -1;
            m_pending_gui_action.weapon_slot = -2;
            m_phase = InputPhase::NONE;
            return;
        }
        if (no_r.contains(mp)) {
            // Player declines → SKIP so arbiter gives weapon to NPC
            m_pending_gui_action.ready       = true;
            m_pending_gui_action.action      = ActionType::SKIP;
            m_pending_gui_action.target_idx  = -1;
            m_pending_gui_action.weapon_slot = -1;
            m_phase = InputPhase::NONE;
            return;
        }
        return; // swallow all other clicks while dialog is open
    }
}