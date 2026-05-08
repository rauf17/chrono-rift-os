#include "inventory.h"

#include <cstring>

static void clearSlot(Weapon& slot) {
    slot.occupied = false;
    slot.name[0] = '\0';
    slot.slot_size = 0;
    slot.damage = 0;
    slot.is_artifact = false;
}

int findContiguousSlots(Entity* entity, int slot_size) {
    if (!entity || slot_size <= 0 || slot_size > INVENTORY_SLOTS) {
        return -1;
    }

    int run = 0;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        if (!entity->inventory[i].occupied) {
            ++run;
            if (run == slot_size) {
                return i - slot_size + 1;
            }
        } else {
            run = 0;
        }
    }

    return -1;
}

int findWeaponStart(Entity* entity, int index) {
    if (!entity || index < 0 || index >= INVENTORY_SLOTS) {
        return -1;
    }

    if (!entity->inventory[index].occupied) {
        return -1;
    }

    for (int i = index; i >= 0; --i) {
        if (!entity->inventory[i].occupied) {
            break;
        }
        if (entity->inventory[i].slot_size > 0) {
            return i;
        }
    }

    return -1;
}

void placeWeapon(Entity* entity, const Weapon& weapon, int start_index) {
    if (!entity) {
        return;
    }

    int size = weapon.slot_size;
    if (size <= 0 || start_index < 0 || start_index + size > INVENTORY_SLOTS) {
        return;
    }

    for (int i = 0; i < size; ++i) {
        Weapon& slot = entity->inventory[start_index + i];
        slot.occupied = true;
        if (i == 0) {
            std::strncpy(slot.name, weapon.name, sizeof(slot.name) - 1);
            slot.name[sizeof(slot.name) - 1] = '\0';
            slot.slot_size = size;
            slot.damage = weapon.damage;
            slot.is_artifact = weapon.is_artifact;
        } else {
            slot.name[0] = '\0';
            slot.slot_size = 0;
            slot.damage = 0;
            slot.is_artifact = false;
        }
    }
}

void swapOutWeapon(Entity* entity, int start_index) {
    if (!entity || start_index < 0 || start_index >= INVENTORY_SLOTS) {
        return;
    }

    int head_index = findWeaponStart(entity, start_index);
    if (head_index < 0) {
        return;
    }

    Weapon& head = entity->inventory[head_index];
    if (!head.occupied) {
        return;
    }

    int size = head.slot_size > 0 ? head.slot_size : 1;
    if (head_index + size > INVENTORY_SLOTS) {
        size = INVENTORY_SLOTS - head_index;
    }

    if (entity->long_term_count < 0 || entity->long_term_count >= LONG_TERM_SIZE) {
        return;
    }

    Weapon& dest = entity->long_term[entity->long_term_count];
    dest = head;
    dest.occupied = true;
    if (dest.slot_size <= 0) {
        dest.slot_size = size;
    }
    entity->long_term_count++;

    // Keep artifact holding flags in sync with inventory contents.
    // These flags are read directly by the Ultimate Ability eligibility check.
    if (std::strcmp(head.name, "Solar Core") == 0) {
        entity->holds_solar_core = false;
    } else if (std::strcmp(head.name, "Lunar Blade") == 0) {
        entity->holds_lunar_blade = false;
    } else if (std::strcmp(head.name, "Eclipse Relic") == 0) {
        entity->holds_eclipse_relic = false;
    }

    for (int i = 0; i < size; ++i) {
        clearSlot(entity->inventory[head_index + i]);
    }
}

void acquireWeapon(Entity* entity, const Weapon& weapon) {
    if (!entity) {
        return;
    }

    int size = weapon.slot_size;
    if (size <= 0 || size > INVENTORY_SLOTS) {
        return;
    }

    int start_index = findContiguousSlots(entity, size);
    if (start_index >= 0) {
        placeWeapon(entity, weapon, start_index);
        // Keep artifact holding flags in sync with inventory contents.
        // These flags are read directly by the Ultimate Ability eligibility check.
        if (std::strcmp(weapon.name, "Solar Core") == 0) {
            entity->holds_solar_core = true;
        } else if (std::strcmp(weapon.name, "Lunar Blade") == 0) {
            entity->holds_lunar_blade = true;
        } else if (std::strcmp(weapon.name, "Eclipse Relic") == 0) {
            entity->holds_eclipse_relic = true;
        }
        return;
    }

    int best_start = -1;
    int best_remove = INVENTORY_SLOTS + 1;

    for (int start = 0; start <= INVENTORY_SLOTS - size; ++start) {
        bool removed[INVENTORY_SLOTS] = {false};
        int count = 0;
        bool invalid = false;

        for (int slot = start; slot < start + size; ++slot) {
            if (!entity->inventory[slot].occupied) {
                continue;
            }
            int head = findWeaponStart(entity, slot);
            if (head < 0) {
                invalid = true;
                break;
            }
            if (entity->inventory[head].is_artifact) {
                invalid = true;
                break;
            }
            if (!removed[head]) {
                removed[head] = true;
                ++count;
            }
        }

        if (!invalid && count < best_remove) {
            best_remove = count;
            best_start = start;
        }
    }

    if (best_start < 0) {
        return;
    }

    bool removed[INVENTORY_SLOTS] = {false};
    for (int slot = best_start; slot < best_start + size; ++slot) {
        if (!entity->inventory[slot].occupied) {
            continue;
        }
        int head = findWeaponStart(entity, slot);
        if (head < 0) {
            continue;
        }
        if (entity->inventory[head].is_artifact) {
            continue;
        }
        if (!removed[head]) {
            removed[head] = true;
            swapOutWeapon(entity, head);
        }
    }

    start_index = findContiguousSlots(entity, size);
    if (start_index >= 0) {
        placeWeapon(entity, weapon, start_index);
        // Keep artifact holding flags in sync with inventory contents.
        // These flags are read directly by the Ultimate Ability eligibility check.
        if (std::strcmp(weapon.name, "Solar Core") == 0) {
            entity->holds_solar_core = true;
        } else if (std::strcmp(weapon.name, "Lunar Blade") == 0) {
            entity->holds_lunar_blade = true;
        } else if (std::strcmp(weapon.name, "Eclipse Relic") == 0) {
            entity->holds_eclipse_relic = true;
        }
    }
}
