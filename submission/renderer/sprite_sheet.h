#pragma once

#include <SFML/Graphics.hpp>
#include <string>

class SpriteSheet {
public:
    explicit SpriteSheet(const std::string& atlas_path);
    void drawEntity(sf::RenderTarget& target, int entity_index, 
                    bool is_player, sf::Vector2f pos);
    void drawWeaponIcon(sf::RenderTarget& target, const char* weapon_name,
                        sf::Vector2f pos);
    void drawStatusIcon(sf::RenderTarget& target, const std::string& icon_name,
                        sf::Vector2f pos);
private:
    sf::Texture m_texture;
    sf::Sprite  m_sprite;
};