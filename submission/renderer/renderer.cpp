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
    snap.wave_number      = state->wave_number;
    snap.enemies_killed   = state->enemies_killed;
    snap.game_won         = state->game_won;
    snap.game_lost        = state->game_lost;

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
    static const sf::Color PLAYER_NAME    {120, 220, 255, 255};
    static const sf::Color PLAYER_OUTLINE {10,  20,  40, 255};
    static const sf::Color ENEMY_NAME     {255, 150,  60, 255};
    static const sf::Color ENEMY_OUTLINE  {40,  10,   0, 255};
    static const sf::Color DEAD_COL       {80,  80,  80,  160};
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
static constexpr float kCardH   = 160.f;
static constexpr float kCardGap = 20.f;
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
static int parseBracketIndex(const GuiButton& btn);

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Renderer::Renderer(const std::string& assets_path,
                   const std::string& window_title)
    : m_sprites(assets_path + "/sprites/sprite.png")
{
    m_window.create(sf::VideoMode(1600, 900), window_title,
                    sf::Style::Titlebar | sf::Style::Close);
    m_window.setVerticalSyncEnabled(true);
    m_window.setFramerateLimit(60);
    m_win_w = 1600.f;
    m_win_h = 900.f;

    if (!m_bg_texture.loadFromFile(assets_path + "/backgrounds/background2.png"))
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

    if (!m_font.loadFromFile(assets_path + "/fonts/VA.ttf"))
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


// ===========================================================================
// LOGIN SCREEN
// ===========================================================================

// ---------------------------------------------------------------------------
// runLoginScreen  – blocking call, returns when PLAY is pressed or window closes
// ---------------------------------------------------------------------------
LoginResult Renderer::runLoginScreen() {
    m_roll_str.clear();
    m_players_str.clear();
    m_login_focus = LoginField::ROLL;
    m_login_err.clear();
    m_login_play_hovered = false;
    m_login_t = 0.f;
    m_clock.restart();

    LoginResult result{};

    while (m_window.isOpen()) {
        float dt = m_clock.restart().asSeconds();
        if (dt > 0.05f) dt = 0.05f;
        m_login_t += dt;

        sf::Event ev{};
        while (m_window.pollEvent(ev)) {
            if (ev.type == sf::Event::Closed) {
                m_window.close();
                return result;  // confirmed = false
            }
            if (ev.type == sf::Event::TextEntered) {
                loginHandleTextEntered(ev.text.unicode);
            }
            if (ev.type == sf::Event::KeyPressed) {
                loginHandleKeyPressed(ev.key.code);
            }
            if (ev.type == sf::Event::MouseMoved) {
                sf::Vector2f mp{(float)ev.mouseMove.x, (float)ev.mouseMove.y};
                // Check play button hover
                const float BW = 220.f, BH = 52.f;
                const float BX = m_win_w / 2.f - BW / 2.f;
                const float BY = m_win_h / 2.f + 140.f;
                m_login_play_hovered = sf::FloatRect{BX, BY, BW, BH}.contains(mp);
            }
            if (ev.type == sf::Event::MouseButtonReleased &&
                ev.mouseButton.button == sf::Mouse::Left) {
                loginHandleClick({(float)ev.mouseButton.x, (float)ev.mouseButton.y});
            }
        }

        // Draw frame
        m_window.clear(sf::Color(6, 4, 14));
        drawLoginScreen(dt);
        m_window.display();

        // Done flag is set by loginHandleClick or Enter key handling
        if (m_login_err == "__PLAY__") {
            result.roll_number  = std::atoi(m_roll_str.c_str());
            result.player_count = std::atoi(m_players_str.c_str());
            result.confirmed    = true;
            return result;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// drawLoginScreen
// ---------------------------------------------------------------------------
void Renderer::drawLoginScreen(float /*dt*/) {
    float t = m_login_t;
    float cx = m_win_w / 2.f;
    float cy = m_win_h / 2.f;

    // --- Background (reuse game background at low opacity) ---
    if (m_bg_texture.getSize().x > 0) {
        m_bg_sprite.setColor(sf::Color(255, 255, 255, 60));
        m_window.draw(m_bg_sprite);
    }

    // --- Animated floating particle orbs ---
    static const float PKX[18] = {
        0.06f,0.14f,0.23f,0.35f,0.48f,0.62f,0.74f,0.85f,0.92f,
        0.10f,0.28f,0.41f,0.55f,0.68f,0.79f,0.88f,0.19f,0.60f
    };
    static const float PKS[18] = {
        0.18f,0.12f,0.22f,0.09f,0.16f,0.20f,0.11f,0.25f,0.14f,
        0.21f,0.13f,0.17f,0.10f,0.23f,0.15f,0.19f,0.08f,0.24f
    };
    static const float PKP[18] = {
        0.0f,0.3f,0.6f,0.1f,0.5f,0.8f,0.2f,0.7f,0.4f,
        0.9f,0.15f,0.45f,0.75f,0.05f,0.35f,0.65f,0.55f,0.85f
    };
    for (int i = 0; i < 18; ++i) {
        float phase = std::fmod(PKP[i] + t * PKS[i], 1.0f);
        float orb_y = phase * m_win_h;
        float orb_x = PKX[i] * m_win_w + 18.f * std::sin(t * 0.8f + i);
        float sz    = 3.f + (i % 3) * 2.f;
        float bright = 0.4f + 0.6f * std::abs(std::sin(t * 0.9f + i));
        sf::Uint8 a = (sf::Uint8)(bright * 140.f);
        sf::Color pc = (i % 3 == 0) ? sf::Color(150, 60, 255, a)
                     : (i % 3 == 1) ? sf::Color(60, 160, 255, a)
                                    : sf::Color(255, 220, 80, a);
        m_rect.setSize({sz, sz});
        m_rect.setPosition(orb_x, orb_y);
        m_rect.setFillColor(pc);
        m_rect.setOutlineThickness(0.f);
        m_window.draw(m_rect);
    }

    // --- Large radial glow behind the panel ---
    {
        sf::CircleShape glow(320.f);
        glow.setOrigin(320.f, 320.f);
        glow.setPosition(cx, cy - 20.f);
        glow.setFillColor(sf::Color(80, 20, 160, 30));
        m_window.draw(glow);
        glow.setRadius(200.f);
        glow.setOrigin(200.f, 200.f);
        glow.setFillColor(sf::Color(100, 30, 200, 40));
        m_window.draw(glow);
    }

    // --- Main panel ---
    const float PW = 560.f, PH = 420.f;
    const float PX = cx - PW / 2.f;
    const float PY = cy - PH / 2.f - 20.f;

    // Outer glow border (animated)
    float border_pulse = 0.5f + 0.5f * std::sin(t * 2.2f);
    sf::Uint8 bp = (sf::Uint8)(140.f + 80.f * border_pulse);
    m_rect.setSize({PW + 10.f, PH + 10.f});
    m_rect.setPosition(PX - 5.f, PY - 5.f);
    m_rect.setFillColor(sf::Color(0, 0, 0, 0));
    m_rect.setOutlineThickness(3.f);
    m_rect.setOutlineColor(sf::Color(160, 60, 255, bp));
    m_window.draw(m_rect);

    // Inner panel
    m_rect.setSize({PW, PH});
    m_rect.setPosition(PX, PY);
    m_rect.setFillColor(sf::Color(8, 5, 20, 235));
    m_rect.setOutlineThickness(1.5f);
    m_rect.setOutlineColor(sf::Color(100, 40, 180, 200));
    m_window.draw(m_rect);

    // Top accent bar
    m_rect.setSize({PW, 3.f});
    m_rect.setPosition(PX, PY);
    m_rect.setFillColor(sf::Color(180, 80, 255, bp));
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);

    // Bottom accent bar
    m_rect.setPosition(PX, PY + PH - 3.f);
    m_window.draw(m_rect);

    // --- Game title inside panel ---
    m_text.setFont(m_font);
    m_text.setCharacterSize(46u);
    m_text.setStyle(sf::Text::Bold);
    m_text.setFillColor(sf::Color(220, 180, 255, 255));
    m_text.setOutlineColor(sf::Color(80, 0, 160, 255));
    m_text.setOutlineThickness(3.f);
    m_text.setString("CHRONO RIFT");
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition(cx - tb.width / 2.f, PY + 18.f);
    }
    m_window.draw(m_text);

    // Subtitle
    m_text.setCharacterSize(13u);
    m_text.setStyle(sf::Text::Regular);
    m_text.setFillColor(sf::Color(140, 100, 200, 200));
    m_text.setOutlineThickness(0.f);
    m_text.setString("WAR  OF  THE  BROKEN  AGE");
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition(cx - tb.width / 2.f, PY + 72.f);
    }
    m_window.draw(m_text);

    // Divider line
    m_rect.setSize({PW - 60.f, 1.f});
    m_rect.setPosition(PX + 30.f, PY + 96.f);
    m_rect.setFillColor(sf::Color(120, 50, 200, 120));
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);

    // --- Input fields ---
    auto drawField = [&](const std::string& label,
                         const std::string& value,
                         float fy, bool focused, bool isRoll) {
        // Label
        m_text.setCharacterSize(13u);
        m_text.setStyle(sf::Text::Regular);
        m_text.setFillColor(focused ? sf::Color(200, 160, 255, 255)
                                    : sf::Color(140, 110, 180, 200));
        m_text.setOutlineThickness(0.f);
        m_text.setString(label);
        m_text.setPosition(PX + 40.f, fy - 22.f);
        m_window.draw(m_text);

        // Field box
        float fw = PW - 80.f;
        float fh = 46.f;
        float fx = PX + 40.f;
        m_rect.setSize({fw, fh});
        m_rect.setPosition(fx, fy);
        m_rect.setFillColor(focused ? sf::Color(25, 12, 50, 230)
                                    : sf::Color(15, 8, 30, 200));
        m_rect.setOutlineThickness(focused ? 2.f : 1.f);
        m_rect.setOutlineColor(focused ? sf::Color(180, 80, 255, 255)
                                       : sf::Color(80, 40, 120, 180));
        m_window.draw(m_rect);

        // Value text
        std::string display = value;
        // Blinking cursor when focused
        if (focused) {
            bool blink = std::fmod(t * 1.5f, 1.0f) < 0.6f;
            if (blink) display += "|";
        }
        m_text.setCharacterSize(22u);
        m_text.setFillColor(sf::Color(230, 210, 255, 255));
        m_text.setString(display.empty() ? (focused ? "|" : "") : display);
        m_text.setPosition(fx + 14.f, fy + 10.f);
        m_window.draw(m_text);

        // Hint text when empty and not focused
        if (value.empty() && !focused) {
            m_text.setCharacterSize(16u);
            m_text.setFillColor(sf::Color(80, 60, 120, 160));
            m_text.setString(isRoll ? "e.g. 23i0591" : "1 – 4");
            m_text.setPosition(fx + 14.f, fy + 13.f);
            m_window.draw(m_text);
        }
    };

    float field1_y = PY + 118.f;
    float field2_y = PY + 218.f;
    drawField("ROLL NUMBER", m_roll_str, field1_y,
              m_login_focus == LoginField::ROLL, true);
    drawField("NUMBER OF PLAYERS", m_players_str, field2_y,
              m_login_focus == LoginField::PLAYERS, false);

    // Player count visual selector (dots)
    {
        float dot_y = field2_y + 56.f;
        float dot_start = PX + 40.f;
        for (int i = 1; i <= 4; ++i) {
            float dot_x = dot_start + (i - 1) * 70.f;
            int chosen = m_players_str.empty() ? 0 : std::atoi(m_players_str.c_str());
            bool sel = (chosen == i);
            sf::CircleShape dot(sel ? 20.f : 16.f);
            dot.setOrigin(dot.getRadius(), dot.getRadius());
            dot.setPosition(dot_x + 20.f, dot_y + 18.f);
            dot.setFillColor(sel ? sf::Color(160, 60, 255, 230)
                                 : sf::Color(40, 20, 80, 180));
            dot.setOutlineThickness(sel ? 2.f : 1.f);
            dot.setOutlineColor(sel ? sf::Color(220, 140, 255, 255)
                                    : sf::Color(100, 60, 160, 180));
            m_window.draw(dot);

            m_text.setCharacterSize(16u);
            m_text.setStyle(sf::Text::Bold);
            m_text.setFillColor(sel ? sf::Color(255, 255, 255, 255)
                                    : sf::Color(160, 120, 200, 200));
            m_text.setOutlineThickness(0.f);
            m_text.setString(std::to_string(i));
            sf::FloatRect tb = m_text.getLocalBounds();
            m_text.setPosition(dot_x + 20.f - tb.width / 2.f - 2.f,
                               dot_y + 18.f - tb.height / 2.f - 4.f);
            m_window.draw(m_text);
        }
        m_text.setStyle(sf::Text::Regular);
    }

    // --- Error message ---
    if (!m_login_err.empty() && m_login_err != "__PLAY__") {
        float err_alpha = std::min(1.f, std::abs(std::sin(t * 4.f))) * 255.f;
        m_text.setCharacterSize(14u);
        m_text.setFillColor(sf::Color(255, 80, 80, (sf::Uint8)err_alpha));
        m_text.setOutlineThickness(0.f);
        m_text.setString(m_login_err);
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition(cx - tb.width / 2.f, PY + PH - 90.f);
        m_window.draw(m_text);
    }

    // --- PLAY button ---
    {
        const float BW = 220.f, BH = 52.f;
        const float BX = cx - BW / 2.f;
        const float BY = PY + PH - 68.f;

        // Button glow when hovered
        if (m_login_play_hovered) {
            m_rect.setSize({BW + 10.f, BH + 10.f});
            m_rect.setPosition(BX - 5.f, BY - 5.f);
            m_rect.setFillColor(sf::Color(120, 40, 255, 80));
            m_rect.setOutlineThickness(0.f);
            m_window.draw(m_rect);
        }

        sf::Color btn_fill = m_login_play_hovered
            ? sf::Color(90, 30, 200, 245)
            : sf::Color(55, 15, 140, 230);
        sf::Color btn_border = m_login_play_hovered
            ? sf::Color(220, 140, 255, 255)
            : sf::Color(160, 80, 240, 200);

        m_rect.setSize({BW, BH});
        m_rect.setPosition(BX, BY);
        m_rect.setFillColor(btn_fill);
        m_rect.setOutlineThickness(2.f);
        m_rect.setOutlineColor(btn_border);
        m_window.draw(m_rect);

        m_text.setCharacterSize(22u);
        m_text.setStyle(sf::Text::Bold);
        m_text.setFillColor(sf::Color(230, 200, 255, 255));
        m_text.setOutlineColor(sf::Color(0, 0, 0, 180));
        m_text.setOutlineThickness(2.f);
        m_text.setString("ENTER THE RIFT");
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition(cx - tb.width / 2.f, BY + (BH - tb.height) / 2.f - 4.f);
        m_window.draw(m_text);
        m_text.setStyle(sf::Text::Regular);
        m_text.setOutlineThickness(0.f);

        // Update hover bounds for click detection
        // (stored as member so loginHandleClick can use it)
        m_login_play_hovered =
            sf::FloatRect{BX, BY, BW, BH}.contains(
                sf::Vector2f(sf::Mouse::getPosition(m_window)));
    }

    // --- Tab hint ---
    m_text.setCharacterSize(11u);
    m_text.setFillColor(sf::Color(80, 60, 120, 160));
    m_text.setString("TAB to switch fields   |   Click dots or type for player count");
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition(cx - tb.width / 2.f, PY + PH + 14.f);
    }
    m_window.draw(m_text);
}

// ---------------------------------------------------------------------------
// loginHandleTextEntered
// ---------------------------------------------------------------------------
void Renderer::loginHandleTextEntered(sf::Uint32 unicode) {
    m_login_err.clear();
    if (unicode > 127) return;
    char c = (char)unicode;

    if (c == 8) {  // backspace
        if (m_login_focus == LoginField::ROLL && !m_roll_str.empty())
            m_roll_str.pop_back();
        else if (m_login_focus == LoginField::PLAYERS && !m_players_str.empty())
            m_players_str.pop_back();
        return;
    }
    if (c < 32) return;  // ignore control chars

    if (m_login_focus == LoginField::ROLL) {
        if (m_roll_str.size() < 12)
            m_roll_str += c;
    } else if (m_login_focus == LoginField::PLAYERS) {
        if (std::isdigit(c) && m_players_str.size() < 1)
            m_players_str = c;  // single digit only
    }
}

// ---------------------------------------------------------------------------
// loginHandleKeyPressed
// ---------------------------------------------------------------------------
void Renderer::loginHandleKeyPressed(sf::Keyboard::Key key) {
    if (key == sf::Keyboard::Tab) {
        m_login_focus = (m_login_focus == LoginField::ROLL)
                        ? LoginField::PLAYERS : LoginField::ROLL;
        m_login_err.clear();
    }
    if (key == sf::Keyboard::Return || key == sf::Keyboard::Enter) {
        // Try to confirm
        int r = 0, p = 0;
        if (loginValidate(r, p))
            m_login_err = "__PLAY__";
    }
}

// ---------------------------------------------------------------------------
// loginHandleClick
// ---------------------------------------------------------------------------
void Renderer::loginHandleClick(sf::Vector2f mp) {
    m_login_err.clear();
    float cx = m_win_w / 2.f;
    float cy = m_win_h / 2.f;
    const float PW = 560.f, PH = 420.f;
    const float PX = cx - PW / 2.f;
    const float PY = cy - PH / 2.f - 20.f;

    // Field 1 (roll)
    if (sf::FloatRect{PX + 40.f, PY + 118.f, PW - 80.f, 46.f}.contains(mp)) {
        m_login_focus = LoginField::ROLL;
        return;
    }
    // Field 2 (players)
    if (sf::FloatRect{PX + 40.f, PY + 218.f, PW - 80.f, 46.f}.contains(mp)) {
        m_login_focus = LoginField::PLAYERS;
        return;
    }
    // Player dots
    float dot_y = PY + 218.f + 56.f;
    float dot_start = PX + 40.f;
    for (int i = 1; i <= 4; ++i) {
        float dot_x = dot_start + (i - 1) * 70.f;
        sf::FloatRect dot_rect{dot_x, dot_y, 40.f, 40.f};
        if (dot_rect.contains(mp)) {
            m_players_str = std::to_string(i);
            return;
        }
    }
    // PLAY button
    const float BW = 220.f, BH = 52.f;
    const float BX = cx - BW / 2.f;
    const float BY = PY + PH - 68.f;
    if (sf::FloatRect{BX, BY, BW, BH}.contains(mp)) {
        int r = 0, p = 0;
        if (loginValidate(r, p))
            m_login_err = "__PLAY__";
    }
}

// ---------------------------------------------------------------------------
// loginValidate
// ---------------------------------------------------------------------------
bool Renderer::loginValidate(int& out_roll, int& out_players) {
    // Parse roll number - accept digits only (strip letters like 23i-0591 → 230591)
    std::string digits;
    for (char c : m_roll_str)
        if (std::isdigit(c)) digits += c;

    if (digits.empty()) {
        m_login_err = "Please enter your roll number.";
        return false;
    }
    out_roll = std::atoi(digits.c_str());

    if (m_players_str.empty()) {
        m_login_err = "Please select number of players (1-4).";
        return false;
    }
    out_players = std::atoi(m_players_str.c_str());
    if (out_players < 1 || out_players > 4) {
        m_login_err = "Player count must be between 1 and 4.";
        return false;
    }
    m_login_err.clear();
    return true;
}

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
        // Keyboard target selection (0-9 keys)
        if (ev.type == sf::Event::KeyPressed && m_phase == InputPhase::TARGET_SELECT) {
            if (ev.key.code >= sf::Keyboard::Num0 && ev.key.code <= sf::Keyboard::Num9) {
                int key_num = ev.key.code - sf::Keyboard::Num0;
                // Try to find target with matching index
                for (const auto& btn : m_target_buttons) {
                    int target_idx = parseBracketIndex(btn);
                    if (target_idx == key_num) {
                        // Select this target
                        int saved_slot = m_pending_gui_action.weapon_slot;
                        m_pending_gui_action.ready       = true;
                        m_pending_gui_action.action      = m_pending_action;
                        m_pending_gui_action.target_idx  = target_idx;
                        m_pending_gui_action.weapon_slot = saved_slot;
                        m_phase = InputPhase::NONE;
                        return true;
                    }
                }
            }
        }
        // Escape closes the victory / game-over end screen gracefully
        if (ev.type == sf::Event::KeyPressed &&
            ev.key.code == sf::Keyboard::Escape) {
            if (m_cached_snap.game_won || m_cached_snap.game_lost) {
                m_window.close();
                return false;
            }
        }
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

void Renderer::triggerWaveBanner(int wave_number) {
    m_wave_banner_active = true;
    m_wave_banner_t      = 0.f;
    m_wave_banner_number = wave_number;
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

    // ---- wave banner timer -----------------------------------------------
    if (m_wave_banner_active) {
        m_wave_banner_t += dt;
        if (m_wave_banner_t >= 3.0f) {
            m_wave_banner_active = false;
            m_wave_banner_t      = 0.f;
        }
    }

    // ---- end screen timer ------------------------------------------------
    m_end_screen_t += dt;
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
        if (snap.entities[i].is_alive && m_anims[i].dead_confirmed) {
            m_anims[i].dead_confirmed = false;
            m_anims[i].death_alpha    = 0.f;
        }
        if (!snap.entities[i].is_my_turn)
            m_anims[i].pulse_t = 0.f;
    }

    m_window.clear(sf::Color(8, 10, 20));
    drawBackground();
    drawBattlefield(snap);

    if (m_wave_banner_active) {
        float alpha_f = 1.f;
        const float TOTAL = 3.0f;
        const float FADEIN = 0.3f;
        const float FADEOUT = 0.4f;
        if (m_wave_banner_t < FADEIN)
            alpha_f = m_wave_banner_t / FADEIN;
        else if (m_wave_banner_t > TOTAL - FADEOUT)
            alpha_f = (TOTAL - m_wave_banner_t) / FADEOUT;
        if (alpha_f < 0.f) alpha_f = 0.f;
        if (alpha_f > 1.f) alpha_f = 1.f;
        sf::Uint8 ov = (sf::Uint8)(120.f * alpha_f);
        m_rect.setSize({m_win_w, m_win_h});
        m_rect.setPosition(0.f, 0.f);
        m_rect.setFillColor(sf::Color(0, 0, 0, ov));
        m_window.draw(m_rect);
    }

    // Floating weapon icons drawn on top of all cards
    drawFloatingWeapons();

    if (m_phase == InputPhase::ACTION_MENU)          drawActionMenu(snap);
    else if (m_phase == InputPhase::TARGET_SELECT)   drawTargetOverlay(snap);
    else if (m_phase == InputPhase::INVENTORY_SELECT) drawInventoryMenu(snap);
    else if (m_phase == InputPhase::LONGTERM_SELECT)  drawLongTermMenu(snap);
    else if (m_phase == InputPhase::DROP_OFFER)       drawDropOfferDialog();

    if (snap.ultimate_active) drawUltimateOverlay();

    // Wave banner drawn on top of everything (except end-screens)
    if (m_wave_banner_active) drawWaveBanner();

    // End-screen overlays (these supersede all other UI)
    if (snap.game_won)       drawVictoryScreen();
    else if (snap.game_lost) drawGameOverScreen();

    drawStatusBar(snap);
    m_window.display();
}

// ---------------------------------------------------------------------------
// drawBackground
// ---------------------------------------------------------------------------
void Renderer::drawBackground() {
    m_bg_sprite.setColor(sf::Color(255, 255, 255, 255));
    m_window.draw(m_bg_sprite);
}

void Renderer::drawText(const std::string& str, float x, float y,
                         unsigned int size, sf::Color col,
                         sf::Color outline)
{
    m_text.setFont(m_font);
    m_text.setString(str);
    m_text.setCharacterSize(size);
    m_text.setFillColor(col);
    m_text.setPosition(x, y);
    m_text.setOutlineColor(outline);
    m_text.setOutlineThickness(2.f);
    m_window.draw(m_text);
    m_text.setStyle(sf::Text::Bold);
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
    // Centre divider
    sf::RectangleShape div({2.f, m_win_h - 200.f});
    div.setPosition(m_win_w / 2.f, 45.f);
    div.setFillColor(sf::Color(C::BORDER_GOLD.r, C::BORDER_GOLD.g,
                                C::BORDER_GOLD.b, 80));
    m_window.draw(div);

    drawText("PLAYERS", 270.f,  200.f, kFontMd, C::PLAYER_NAME,
             C::PLAYER_OUTLINE);
    drawText("ENEMIES", 1150.f, 200.f, kFontMd, C::ENEMY_NAME,
             C::ENEMY_OUTLINE);

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
    int e_cols = 2;

    for (int idx = 0; idx < p_count; ++idx) {
        int row = idx / p_cols;
        int col = idx % p_cols;
        float cx = kLeftX + col * (kCardW + kCardGap) + 150.f;
        float cy = 250.f + row * (kCardH + kCardGap);
        int ent_idx = player_ids[idx];
        const auto& ent = snap.entities[ent_idx];

        // Store card centre for animation position interpolation
        m_entity_pos[ent_idx] = {cx + kCardW / 2.f, cy + kCardH / 2.f};

        const char* override_name = nullptr;
        if (p_cols == 2 && p_count > 1 && (idx == 0 || idx == 1 || idx == 2 || idx == 3)) {
            int swap_idx = (idx % 2 == 0) ? idx + 1 : idx - 1;
            if (swap_idx < p_count)
                override_name = snap.entities[player_ids[swap_idx]].name;
        }

        float draw_x = cx + m_anims[ent_idx].attack_dx;
        drawEntityCard(ent, ent_idx, {draw_x, cy},
                       snap.current_turn_idx == ent_idx,
                       override_name);
    }

    for (int idx = 0; idx < e_count; ++idx) {
        int row = idx / e_cols;
        int col = idx % e_cols;
        float cx = 1050.f + col * (kCardW + kCardGap);
        float cy = 250.f + row * (kCardH + kCardGap);
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
                               bool highlight,
                               const char* override_name)
{
    const float W = kCardW;
    const float H = kCardH;
    auto& anim = m_anims[entity_index];

    // Sprite with turn-pulse scale
    float scale_bump  = (highlight && ent.is_alive)
                        ? 1.f + 0.04f * std::sin(anim.pulse_t) : 1.f;
    float sprite_base = 110.f;
    float sprite_size = sprite_base * scale_bump;
    float sprite_x    = pos.x + (W - sprite_size) / 2.f;
    float sprite_y    = pos.y + 30.f;

    sf::Color name_col = ent.is_player ? C::PLAYER_NAME : C::ENEMY_NAME;
    if (!ent.is_alive) name_col = C::DEAD_COL;
    drawText(override_name ? override_name : ent.name,
             pos.x + 4.f, pos.y + 20.f, kFontSm, name_col,
             ent.is_player ? C::PLAYER_OUTLINE : C::ENEMY_OUTLINE);

    m_sprites.drawEntity(m_window, entity_index, ent.is_player,
                          {sprite_x, sprite_y},
                          {sprite_size, sprite_size});

    if (highlight && ent.is_alive) {
        sf::CircleShape ring(36.f);
        ring.setOrigin(36.f, 36.f);
        ring.setScale(1.f, 0.35f);
        ring.setPosition(sprite_x + sprite_size / 2.f,
                         sprite_y + sprite_size);
        float pulse = 0.5f + 0.5f * std::sin(anim.pulse_t);
        sf::Uint8 alpha = (sf::Uint8)(120 + 135.f * pulse);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(5.f);
        ring.setOutlineColor(sf::Color(0, 255, 80, alpha));
        m_window.draw(ring);
    }

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

    if (!ent.is_alive) {
        drawText("DEAD", pos.x + W / 2.f - 16.f, pos.y + H / 2.f - 8.f,
                 kFontMd, sf::Color(180, 30, 30, 200));
        return;
    }

    float hp_y = pos.y + sprite_size + 40.f;
    drawHpBar(pos.x + 4.f, hp_y, W * 0.6f, 8.f, ent.hp, ent.max_hp);
    char hpbuf[32];
    std::snprintf(hpbuf, sizeof(hpbuf), "%d/%d", ent.hp, ent.max_hp);
    drawText(hpbuf, pos.x + 4.f, hp_y + 2.f, kFontSm - 1,
             sf::Color(255, 255, 255, 220));

    float stam_y = hp_y + 20.f;
    drawStaminaBar(pos.x + 4.f, stam_y, W * 0.6f, 6.f,
                   ent.stamina, ent.max_stamina);
    int sp = ent.max_stamina > 0.f
             ? (int)(ent.stamina / ent.max_stamina * 100.f) : 0;
    char sbuf[16];
    std::snprintf(sbuf, sizeof(sbuf), "SP %d%%", sp);
    drawText(sbuf, pos.x + W * 0.6f - 40.f, stam_y + 2.f,
             kFontSm - 1, sf::Color(255, 255, 255, 220));

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
        float icon_x = pos.x + 4.f + drawn * 22.f;
        float icon_y = pos.y + 50.f;
        float ring_size = 22.f;

        m_rect.setSize({ring_size, ring_size});
        m_rect.setPosition(icon_x - 2.f, icon_y - 2.f);
        m_rect.setFillColor(sf::Color(0, 0, 0, 255));
        m_rect.setOutlineThickness(2.f);
        m_rect.setOutlineColor(C::BORDER_GOLD);
        m_window.draw(m_rect);

        m_sprites.drawWeaponIcon(m_window, slot.name,
                                  {icon_x, icon_y},
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
            // Calculate player number for players (1-indexed)
            if (e.is_player) {
                // Build player_ids array to match battlefield logic
                int player_ids[MAX_ENTITIES] = {};
                int p_count = 0;
                int total = snap.player_count + snap.npc_count;
                for (int i = 0; i < total && i < MAX_ENTITIES; ++i) {
                    if (snap.entities[i].is_player)
                        player_ids[p_count++] = i;
                }
                
                // Find which position in player_ids array this entity is at
                int logical_pos = -1;
                for (int i = 0; i < p_count; ++i) {
                    if (player_ids[i] == idx) {
                        logical_pos = i;
                        break;
                    }
                }
                
                int display_pos = logical_pos;
                // Apply swap logic if there are more than 3 players (2 columns)
                int p_cols = (p_count > 3) ? 2 : 1;
                if (p_cols == 2 && p_count > 1 && logical_pos >= 0 && logical_pos <= 3) {
                    display_pos = (logical_pos % 2 == 0) ? logical_pos + 1 : logical_pos - 1;
                }
                
                int player_num = display_pos + 1;
                char pbuf[32];
                std::snprintf(pbuf, sizeof(pbuf), "Player %d's turn", player_num);
                msg = pbuf;
                if (m_phase != InputPhase::NONE)
                    msg += "  —  Choose an action";
            } else {
                msg = std::string(e.name) + "'s turn";
                msg += "  —  Enemy is thinking...";
            }
        }
        drawText(msg, 12.f, sy + 10.f, kFontMd, C::WHITE);
    }
    if (snap.ultimate_active)
        drawText("ULTIMATE ACTIVE", m_win_w - 400.f, sy + 10.f,
                 kFontMd, sf::Color(255, 220, 60, 255));

    // Wave indicator and kill counter
    char wave_buf[64];
    std::snprintf(wave_buf, sizeof(wave_buf), "Wave %d   Kills: %d/10   [Press 0-9 to select target]",
                  snap.wave_number, snap.enemies_killed);
    drawText(wave_buf, m_win_w / 2.f - 80.f, sy + 10.f,
             kFontMd, sf::Color(180, 255, 180, 255));
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

static int parseBracketIndex(const GuiButton& btn) {
    const std::string& lbl = btn.label;
    size_t open = lbl.find('[');
    size_t close = lbl.find(']');
    if (open != std::string::npos && close != std::string::npos && close > open) {
        int idx = 0;
        std::sscanf(lbl.c_str() + open + 1, "%d", &idx);
        return idx;
    }
    return -1;
}

void Renderer::buildActionButtons(const RenderSnapshot::EntitySnap& actor) {
    m_action_buttons.clear();
    const float BW = 148.f, BH = 34.f, GAP = 6.f;
    const float MX = (m_win_w - (BW * 4.f + GAP * 3.f)) / 2.f;
    const float MY = 716.f;

    struct { const char* label; bool enabled; } acts[] = {
        {"Strike",     true},
        {"Exhaust",    true},
        {"Heal",       true},
        {"Skip",       true},
        {"Use Weapon", true},
        {"Swap In",    actor.long_term_count > 0},
        {"Ultimate",   actor.holds_solar_core && actor.holds_lunar_blade},
        {"Stun",       true},
    };

    for (int i = 0; i < 8; ++i) {
        int col = i % 4;
        int row = i / 4;
        m_action_buttons.push_back(
            makeButton(MX + col * (BW + GAP), MY + row * (BH + GAP),
                       BW, BH, acts[i].label, acts[i].enabled));
    }

    float quit_x = m_win_w / 2.f - BW / 2.f;
    float quit_y = 804.f;
    m_action_buttons.push_back(
        makeButton(quit_x, quit_y, BW, BH, "Quit", true));
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
    float panel_y = 700.f;
    drawPanelBackground(0.f, panel_y, m_win_w, m_win_h - panel_y, C::MENU_BG);
    drawText("Choose Action:", 12.f, 704.f, kFontMd, C::BORDER_GOLD);
    for (auto& btn : m_action_buttons) {
        if (std::strcmp(btn.label.c_str(), "Quit") == 0) {
            sf::Color fill = btn.hovered ? sf::Color(180, 50, 50, 255)
                                          : sf::Color(120, 30, 30, 230);
            m_rect.setSize({btn.bounds.width, btn.bounds.height});
            m_rect.setPosition(btn.bounds.left, btn.bounds.top);
            m_rect.setFillColor(fill);
            m_rect.setOutlineThickness(1.5f);
            m_rect.setOutlineColor(C::BORDER_GOLD);
            m_window.draw(m_rect);
            m_text.setFont(m_font);
            m_text.setString(btn.label);
            m_text.setCharacterSize(13u);
            m_text.setFillColor(btn.enabled ? C::BTN_TEXT : C::DEAD_COL);
            sf::FloatRect tb = m_text.getLocalBounds();
            m_text.setPosition(
                btn.bounds.left + (btn.bounds.width  - tb.width)  / 2.f,
                btn.bounds.top  + (btn.bounds.height - tb.height) / 2.f - 2.f);
            m_window.draw(m_text);
        } else {
            drawButton(m_window, m_rect, m_text, m_font, btn);
        }
    }
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
// drawWaveBanner  – "WAVE N INCOMING!" displayed for ~3 seconds centre-screen
// ---------------------------------------------------------------------------
void Renderer::drawWaveBanner() {
    // Fade in over 0.3 s, hold, fade out over 0.4 s at end.
    const float TOTAL  = 3.0f;
    const float FADEIN = 0.3f;
    const float FADEOUT= 0.4f;

    float t = m_wave_banner_t;
    float alpha_f = 1.f;
    if (t < FADEIN)
        alpha_f = t / FADEIN;
    else if (t > TOTAL - FADEOUT)
        alpha_f = (TOTAL - t) / FADEOUT;
    if (alpha_f < 0.f) alpha_f = 0.f;
    if (alpha_f > 1.f) alpha_f = 1.f;
    sf::Uint8 a = (sf::Uint8)(alpha_f * 255.f);

    // Semi-transparent dark panel
    const float PW = 600.f, PH = 120.f;
    const float PX = (m_win_w - PW) / 2.f;
    const float PY = (m_win_h - PH) / 2.f - 40.f;

    m_rect.setSize({PW, PH});
    m_rect.setPosition(PX, PY);
    m_rect.setFillColor(sf::Color(5, 8, 20, (sf::Uint8)(alpha_f * 210.f)));
    m_rect.setOutlineThickness(3.f);
    m_rect.setOutlineColor(sf::Color(255, 80, 30, a));
    m_window.draw(m_rect);

    // "WAVE N" in large red-orange text
    char line1[64];
    std::snprintf(line1, sizeof(line1), "WAVE %d", m_wave_banner_number);
    m_text.setFont(m_font);
    m_text.setString(line1);
    m_text.setCharacterSize(52u);
    m_text.setFillColor(sf::Color(255, 90, 20, a));
    m_text.setOutlineColor(sf::Color(0, 0, 0, a));
    m_text.setOutlineThickness(3.f);
    m_text.setStyle(sf::Text::Bold);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 8.f);
    }
    m_window.draw(m_text);

    // Sub-line "INCOMING!" in white
    m_text.setString("INCOMING!");
    m_text.setCharacterSize(26u);
    m_text.setFillColor(sf::Color(255, 220, 180, a));
    m_text.setOutlineColor(sf::Color(0, 0, 0, a));
    m_text.setOutlineThickness(2.f);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 72.f);
    }
    m_window.draw(m_text);
    m_text.setStyle(sf::Text::Regular);
}

// ---------------------------------------------------------------------------
// drawVictoryScreen  – beautiful "CONGRATULATIONS YOU WON!" popup
// ---------------------------------------------------------------------------
void Renderer::drawVictoryScreen() {
    // Advance own timer via m_end_screen_t (already ticked in stepAnimations)
    float t = m_end_screen_t;

    // Pulsing gold border alpha
    float pulse = 0.5f + 0.5f * std::sin(t * 3.f);
    sf::Uint8 ba = (sf::Uint8)(160.f + 90.f * pulse);

    // Dark vignette overlay
    m_rect.setSize({m_win_w, m_win_h});
    m_rect.setPosition(0.f, 0.f);
    m_rect.setFillColor(sf::Color(0, 0, 0, 180));
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);

    // Glowing border strips
    auto glow = [&](float x, float y, float w, float h) {
        m_rect.setSize({w, h});
        m_rect.setPosition(x, y);
        m_rect.setFillColor(sf::Color(255, 215, 0, ba));
        m_rect.setOutlineThickness(0.f);
        m_window.draw(m_rect);
    };
    const float BW = 6.f;
    glow(0, 0, m_win_w, BW);
    glow(0, m_win_h - BW, m_win_w, BW);
    glow(0, 0, BW, m_win_h);
    glow(m_win_w - BW, 0, BW, m_win_h);

    // Central panel
    const float PW = 760.f, PH = 280.f;
    const float PX = (m_win_w - PW) / 2.f;
    const float PY = (m_win_h - PH) / 2.f;
    m_rect.setSize({PW, PH});
    m_rect.setPosition(PX, PY);
    m_rect.setFillColor(sf::Color(8, 18, 8, 240));
    m_rect.setOutlineThickness(4.f);
    m_rect.setOutlineColor(sf::Color(255, 215, 0, ba));
    m_window.draw(m_rect);

    // "CONGRATULATIONS!" title
    m_text.setFont(m_font);
    m_text.setString("CONGRATULATIONS!");
    m_text.setCharacterSize(50u);
    m_text.setFillColor(sf::Color(255, 215, 0, 255));
    m_text.setOutlineColor(sf::Color(0, 0, 0, 255));
    m_text.setOutlineThickness(3.f);
    m_text.setStyle(sf::Text::Bold);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 18.f);
    }
    m_window.draw(m_text);

    // "YOU WON!" in bright green
    m_text.setString("YOU WON!");
    m_text.setCharacterSize(64u);
    m_text.setFillColor(sf::Color(80, 255, 100, 255));
    m_text.setOutlineColor(sf::Color(0, 0, 0, 255));
    m_text.setOutlineThickness(4.f);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 88.f);
    }
    m_window.draw(m_text);

    // "All enemies defeated!" subtitle
    m_text.setString("All enemies defeated!");
    m_text.setCharacterSize(26u);
    m_text.setFillColor(sf::Color(200, 255, 200, 230));
    m_text.setOutlineColor(sf::Color(0, 0, 0, 200));
    m_text.setOutlineThickness(2.f);
    m_text.setStyle(sf::Text::Regular);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 178.f);
    }
    m_window.draw(m_text);

    // "Close window to exit" hint (dim)
    m_text.setString("Press ESC or close the window to exit.");
    m_text.setCharacterSize(18u);
    m_text.setFillColor(sf::Color(160, 160, 160, 200));
    m_text.setOutlineThickness(0.f);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 230.f);
    }
    m_window.draw(m_text);
}

// ---------------------------------------------------------------------------
// drawGameOverScreen  – "GAME OVER – All players defeated"
// ---------------------------------------------------------------------------
void Renderer::drawGameOverScreen() {
    float t = m_end_screen_t;
    float pulse = 0.5f + 0.5f * std::sin(t * 2.5f);
    sf::Uint8 ba = (sf::Uint8)(140.f + 100.f * pulse);

    // Blood-red vignette
    m_rect.setSize({m_win_w, m_win_h});
    m_rect.setPosition(0.f, 0.f);
    m_rect.setFillColor(sf::Color(20, 0, 0, 170));
    m_rect.setOutlineThickness(0.f);
    m_window.draw(m_rect);

    // Pulsing red border
    auto glow = [&](float x, float y, float w, float h) {
        m_rect.setSize({w, h});
        m_rect.setPosition(x, y);
        m_rect.setFillColor(sf::Color(200, 20, 20, ba));
        m_rect.setOutlineThickness(0.f);
        m_window.draw(m_rect);
    };
    const float BW = 6.f;
    glow(0, 0, m_win_w, BW);
    glow(0, m_win_h - BW, m_win_w, BW);
    glow(0, 0, BW, m_win_h);
    glow(m_win_w - BW, 0, BW, m_win_h);

    // Central panel
    const float PW = 700.f, PH = 260.f;
    const float PX = (m_win_w - PW) / 2.f;
    const float PY = (m_win_h - PH) / 2.f;
    m_rect.setSize({PW, PH});
    m_rect.setPosition(PX, PY);
    m_rect.setFillColor(sf::Color(18, 4, 4, 245));
    m_rect.setOutlineThickness(4.f);
    m_rect.setOutlineColor(sf::Color(200, 20, 20, ba));
    m_window.draw(m_rect);

    // "GAME OVER"
    m_text.setFont(m_font);
    m_text.setString("GAME OVER");
    m_text.setCharacterSize(60u);
    m_text.setFillColor(sf::Color(220, 30, 30, 255));
    m_text.setOutlineColor(sf::Color(0, 0, 0, 255));
    m_text.setOutlineThickness(4.f);
    m_text.setStyle(sf::Text::Bold);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 20.f);
    }
    m_window.draw(m_text);

    // "All players defeated."
    m_text.setString("All players defeated.");
    m_text.setCharacterSize(30u);
    m_text.setFillColor(sf::Color(255, 160, 160, 230));
    m_text.setOutlineColor(sf::Color(0, 0, 0, 200));
    m_text.setOutlineThickness(2.f);
    m_text.setStyle(sf::Text::Regular);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 110.f);
    }
    m_window.draw(m_text);

    // "Close window to exit"
    m_text.setString("Press ESC or close the window to exit.");
    m_text.setCharacterSize(18u);
    m_text.setFillColor(sf::Color(160, 100, 100, 200));
    m_text.setOutlineThickness(0.f);
    {
        sf::FloatRect tb = m_text.getLocalBounds();
        m_text.setPosition((m_win_w - tb.width) / 2.f, PY + 210.f);
    }
    m_window.draw(m_text);
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
            if (btn.bounds.contains(mp)) {
                int entity_idx = parseBracketIndex(btn);
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
            if (btn.enabled && btn.bounds.contains(mp)) {
                m_pending_gui_action.weapon_slot = parseBracketIndex(btn);
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

    // ---- LONG-TERM SELECT --------------------------------------------------
    if (m_phase == InputPhase::LONGTERM_SELECT) {
        const float BW = 230.f, MX = m_win_w/2.f - BW/2.f;
        float by = 100.f;
        for (auto& btn : m_longterm_buttons) {
            if (btn.enabled && btn.bounds.contains(mp)) {
                m_pending_gui_action.ready       = true;
                m_pending_gui_action.action      = ActionType::SWAP_IN;
                m_pending_gui_action.target_idx  = -1;
                m_pending_gui_action.weapon_slot = parseBracketIndex(btn);
                m_phase = InputPhase::NONE;
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