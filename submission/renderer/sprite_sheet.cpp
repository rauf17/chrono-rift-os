#include "sprite_sheet.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
SpriteSheet::SpriteSheet(const std::string& atlas_path) {
    if (!m_texture.loadFromFile(atlas_path)) {
        std::fprintf(stderr,
            "SpriteSheet: WARNING – could not load atlas '%s'. "
            "Sprites will render as coloured fallback rectangles.\n",
            atlas_path.c_str());
        // We still continue – drawSprite will detect the empty texture and
        // fall back to a coloured rectangle so the game is still playable.
    }

    // Pixel art: disable smoothing so sprites stay crisp at any scale.
    m_texture.setSmooth(false);
    m_sprite.setTexture(m_texture, /*resetRect=*/true);
}

// ---------------------------------------------------------------------------
// Grid helpers – return the source IntRect for a given row/column
// ---------------------------------------------------------------------------
sf::IntRect SpriteSheet::getPlayerRect(int col) const {
    // Row 0: y=0, cell 512x512, 4 columns
    return sf::IntRect(col * kCharCellW, kRow0Y, kCharCellW, kCharCellH);
}

sf::IntRect SpriteSheet::getEnemyRect(int col) const {
    // Row 1: y=512, cell 512x512, 4 columns (col 3 is empty – never requested)
    return sf::IntRect(col * kCharCellW, kRow1Y, kCharCellW, kCharCellH);
}

sf::IntRect SpriteSheet::getWeaponRect(int col) const {
    // Row 2: y=1024, cell 256x512, 8 columns
    return sf::IntRect(col * kWeapCellW, kRow2Y, kWeapCellW, kWeapCellH);
}

sf::IntRect SpriteSheet::getStatusRect(int col) const {
    // Row 3: y=1536, cell 512x512, 4 columns (col 3 ignored)
    return sf::IntRect(col * kCharCellW, kRow3Y, kCharCellW, kCharCellH);
}

// ---------------------------------------------------------------------------
// Internal draw helper
// ---------------------------------------------------------------------------
void SpriteSheet::drawSprite(sf::RenderTarget& target,
                              sf::IntRect       src_rect,
                              sf::Vector2f      pos,
                              sf::Vector2f      display_size)
{
    // If texture failed to load, draw a coloured placeholder rectangle.
    if (m_texture.getSize().x == 0) {
        sf::RectangleShape fallback(display_size);
        fallback.setPosition(pos);
        fallback.setFillColor(sf::Color(120, 80, 160, 180));
        fallback.setOutlineThickness(1.f);
        fallback.setOutlineColor(sf::Color(200, 200, 200, 100));
        target.draw(fallback);
        return;
    }

    m_sprite.setTextureRect(src_rect);

    // Scale the source cell to the requested display size.
    float sx = display_size.x / static_cast<float>(src_rect.width);
    float sy = display_size.y / static_cast<float>(src_rect.height);
    m_sprite.setScale(sx, sy);
    m_sprite.setPosition(pos);
    m_sprite.setColor(sf::Color::White); // reset any tint
    target.draw(m_sprite);
}

// ---------------------------------------------------------------------------
// drawEntity
// ---------------------------------------------------------------------------
void SpriteSheet::drawEntity(sf::RenderTarget& target,
                              int               entity_index,
                              bool              is_player,
                              sf::Vector2f      pos,
                              sf::Vector2f      display_size)
{
    sf::IntRect rect;

    if (is_player) {
        // Players: entity index 0-3 maps directly to columns 0-3 in row 0.
        // Clamp to [0,3] in case index is somehow out of range.
        int col = entity_index;
        if (col < 0) col = 0;
        if (col > 3) col = 3;
        rect = getPlayerRect(col);
        drawSprite(target, rect, pos, display_size);
    } else {
        // Enemies: cycle across columns 0-2 of row 1 (column 3 is empty).
        // entity_index here is the absolute entity index; we derive NPC-local
        // index by keeping modulo 3 so sprites cycle: Orc / Skeleton / Golem.
        int col = entity_index % 3;
        rect = getEnemyRect(col);
        sf::Vector2f flipped_pos = {pos.x + display_size.x, pos.y};
        sf::Vector2f flipped_size = {-display_size.x, display_size.y};
        drawSprite(target, rect, flipped_pos, flipped_size);
    }
}

// ---------------------------------------------------------------------------
// drawWeaponIcon
// ---------------------------------------------------------------------------
void SpriteSheet::drawWeaponIcon(sf::RenderTarget& target,
                                  const char*       weapon_name,
                                  sf::Vector2f      pos,
                                  sf::Vector2f      display_size)
{
    if (!weapon_name || weapon_name[0] == '\0') {
        return;
    }

    // Map weapon name -> column in row 2.
    // Row 2 layout:
    //  col 0 – Solar Core
    //  col 1 – Lunar Blade
    //  col 2 – Iron Halberd
    //  col 3 – Venom Dagger
    //  col 4 – Thunderstaff
    //  col 5 – Obsidian Axe
    //  col 6 – Frostbow
    //  col 7 – Splinter Stick

    int col = -1;

    if      (std::strcmp(weapon_name, "Solar Core")    == 0) col = 0;
    else if (std::strcmp(weapon_name, "Lunar Blade")   == 0) col = 1;
    else if (std::strcmp(weapon_name, "Iron Halberd")  == 0) col = 2;
    else if (std::strcmp(weapon_name, "Venom Dagger")  == 0) col = 3;
    else if (std::strcmp(weapon_name, "Thunderstaff")  == 0) col = 4;
    else if (std::strcmp(weapon_name, "Obsidian Axe")  == 0) col = 5;
    else if (std::strcmp(weapon_name, "Frostbow")      == 0) col = 6;
    else if (std::strcmp(weapon_name, "Splinter Stick")== 0) col = 7;

    if (col < 0) {
        // Unknown weapon name – draw a grey placeholder.
        sf::RectangleShape fallback(display_size);
        fallback.setPosition(pos);
        fallback.setFillColor(sf::Color(100, 100, 100, 160));
        target.draw(fallback);
        return;
    }

    drawSprite(target, getWeaponRect(col), pos, display_size);
}

// ---------------------------------------------------------------------------
// drawStatusIcon
// ---------------------------------------------------------------------------
void SpriteSheet::drawStatusIcon(sf::RenderTarget&  target,
                                  const std::string& icon_name,
                                  sf::Vector2f       pos,
                                  sf::Vector2f       display_size)
{
    // Row 3 layout:
    //  col 0 – Stunned  (lightning bolt + stars)
    //  col 1 – Ultimate (golden swirl)
    //  col 2 – Turn     (green arrow)
    //  col 3 – ignored

    int col = -1;

    if      (icon_name == "stunned")  col = 0;
    else if (icon_name == "ultimate") col = 1;
    else if (icon_name == "turn")     col = 2;

    if (col < 0) {
        // Unrecognised icon – draw nothing.
        return;
    }

    drawSprite(target, getStatusRect(col), pos, display_size);
}