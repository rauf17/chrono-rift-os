#pragma once

#include "../shared/shared_state.h"

int findContiguousSlots(Entity* entity, int slot_size);
int findWeaponStart(Entity* entity, int index);
void placeWeapon(Entity* entity, const Weapon& weapon, int start_index);
void swapOutWeapon(Entity* entity, int start_index);
void acquireWeapon(Entity* entity, const Weapon& weapon);
