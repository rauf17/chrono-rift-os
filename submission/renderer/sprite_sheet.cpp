#include "sprite_sheet.h"

SpriteSheet::SpriteSheet(const std::string& atlas_path) {
    if (!m_texture.loadFromFile(atlas_path)) {
        // Just continue, don't crash
    }
}

void SpriteSheet::drawEntity(sf::RenderTarget& target, int entity_index, 
                             bool is_player, sf::Vector2f pos) {
    sf::RectangleShape rect(sf::Vector2f(40, 40));
    if (is_player) {
        rect.setFillColor(sf::Color(80, 140, 200));
    } else {
        rect.setFillColor(sf::Color(200, 80, 80));
    }
    rect.setPosition(pos);
    target.draw(rect);
}

void SpriteSheet::drawWeaponIcon(sf::RenderTarget& target, const char* weapon_name,
                                 sf::Vector2f pos) {
    sf::RectangleShape rect(sf::Vector2f(18, 18));
    rect.setFillColor(sf::Color(200, 180, 60));
    rect.setPosition(pos);
    target.draw(rect);
}

void SpriteSheet::drawStatusIcon(sf::RenderTarget& target, const std::string& icon_name,
                                 sf::Vector2f pos) {
    sf::RectangleShape rect(sf::Vector2f(18, 18));
    if (icon_name == "stunned") {
        rect.setFillColor(sf::Color::Green);
    } else if (icon_name == "ultimate") {
        rect.setFillColor(sf::Color::Yellow);
    } else if (icon_name == "turn") {
        rect.setFillColor(sf::Color::White);
    } else {
        rect.setFillColor(sf::Color(128, 128, 128));
    }
    rect.setPosition(pos);
    target.draw(rect);
}