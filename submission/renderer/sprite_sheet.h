#pragma once

#include <SFML/Graphics.hpp>
#include <string>

// ---------------------------------------------------------------------------
// SpriteSheet
// ---------------------------------------------------------------------------
// Owns the sprite atlas texture and exposes three draw helpers used by
// Renderer.  The atlas is 2048x2048 with a NON-UNIFORM grid:
//
//  Row 0  (y=   0, h=512) – 4 cols @ 512 wide  – Player characters
//  Row 1  (y= 512, h=512) – 4 cols @ 512 wide  – Enemy characters
//  Row 2  (y=1024, h=512) – 8 cols @ 256 wide  – Weapon icons
//  Row 3  (y=1536, h=512) – 4 cols @ 512 wide  – Status / UI icons
//
// Player columns  (row 0):  0=Knight  1=Wizard  2=Rogue  3=Paladin
// Enemy columns   (row 1):  0=Orc     1=Skeleton Mage   2=Golem   3=empty
// Weapon columns  (row 2):  0=Solar Core  1=Lunar Blade  2=Iron Halberd
//                            3=Venom Dagger 4=Thunderstaff 5=Obsidian Axe
//                            6=Frostbow    7=Splinter Stick
// Status columns  (row 3):  0=Stunned  1=Ultimate  2=Turn Arrow  3=ignored
// ---------------------------------------------------------------------------

class SpriteSheet {
public:
    // atlas_path – path to sprites.png (e.g. "../assets/sprites/sprites.png")
    explicit SpriteSheet(const std::string& atlas_path);

    // Draw the character sprite for the given entity.
    // entity_index – absolute entity index (0..MAX_ENTITIES-1)
    // is_player    – true for player characters, false for NPCs
    // pos          – top-left pixel position on the render target
    // display_size – size to draw the sprite (default 40x40)
    void drawEntity(sf::RenderTarget& target,
                    int entity_index,
                    bool is_player,
                    sf::Vector2f pos,
                    sf::Vector2f display_size = {40.f, 40.f});

    // Draw a small weapon icon.
    // weapon_name – must match one of the eight weapon name strings exactly
    // pos         – top-left pixel position
    // display_size – size to draw (default 20x20)
    void drawWeaponIcon(sf::RenderTarget& target,
                        const char* weapon_name,
                        sf::Vector2f pos,
                        sf::Vector2f display_size = {20.f, 20.f});

    // Draw a status / UI icon.
    // icon_name – "stunned" | "ultimate" | "turn"
    // pos       – top-left pixel position
    // display_size – size to draw (default 20x20)
    void drawStatusIcon(sf::RenderTarget& target,
                        const std::string& icon_name,
                        sf::Vector2f pos,
                        sf::Vector2f display_size = {20.f, 20.f});

private:
    // Return the source IntRect for a character/icon/weapon cell.
    sf::IntRect getPlayerRect(int col)  const;
    sf::IntRect getEnemyRect(int col)   const;
    sf::IntRect getWeaponRect(int col)  const;
    sf::IntRect getStatusRect(int col)  const;

    // Internal draw helper: set rect + scale, then draw.
    void drawSprite(sf::RenderTarget& target,
                    sf::IntRect src_rect,
                    sf::Vector2f pos,
                    sf::Vector2f display_size);

    sf::Texture m_texture;
    sf::Sprite  m_sprite;

    // Grid metrics (const after construction)
    static constexpr int kCharCellW  = 512;
    static constexpr int kCharCellH  = 512;
    static constexpr int kWeapCellW  = 256;
    static constexpr int kWeapCellH  = 512;

    static constexpr int kRow0Y = 0;
    static constexpr int kRow1Y = 512;
    static constexpr int kRow2Y = 1024;
    static constexpr int kRow3Y = 1536;
};