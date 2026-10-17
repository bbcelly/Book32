// Host test for Inkade's gamepad button mapping.
// Build: c++ -std=c++17 -I lib/Book32_Core test/pad_mapping_host/test.cpp
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "PadMapping.h"

using namespace HidGamepad;
using namespace PadMapping;

int main() {
    // Defaults: each action on the button of the same name, Home pauses.
    Map map = DEFAULTS;
    assert(gameButtons(map, PAD_A) == 1 && gameButtons(map, PAD_B) == 2);
    assert(gameButtons(map, PAD_SELECT) == 4 && gameButtons(map, PAD_START) == 8);
    assert(gameButtons(map, PAD_RIGHT | PAD_UP) == (0x10 | 0x40));
    assert(gameButtons(map, PAD_LEFT | PAD_DOWN) == (0x20 | 0x80));
    assert(gameButtons(map, PAD_X | PAD_Y | PAD_L | PAD_R | PAD_L2 | PAD_R2 | PAD_HOME) == 0);
    assert(pauses(map, PAD_HOME) && !pauses(map, PAD_START) && pauses(map, PAD_SELECT | PAD_START));
    assert(isDefault(map));

    // A free button replaces the action's button outright: one button each.
    assert(assign(map, GAME_A, PAD_R2) == -1);
    assert(map.masks[GAME_A] == PAD_R2 && gameButtons(map, PAD_R2) == 1 && gameButtons(map, PAD_A) == 0);

    // A button already in use swaps: B takes X, and nothing else changes.
    assign(map, GAME_A, PAD_X);
    assert(assign(map, GAME_B, PAD_X) == GAME_A);
    assert(map.masks[GAME_B] == PAD_X && map.masks[GAME_A] == PAD_B);
    assert(gameButtons(map, PAD_X) == 2 && gameButtons(map, PAD_B) == 1);

    // Swapping with the pause button keeps a pause button.
    map = DEFAULTS;
    assert(assign(map, PAUSE, PAD_START) == GAME_START);
    assert(map.masks[PAUSE] == PAD_START && map.masks[GAME_START] == PAD_HOME && pauses(map, PAD_START));

    // Its own button again, out-of-range actions and no button change nothing.
    Map before = map;
    assert(assign(map, PAUSE, PAD_START) == -1);
    assign(map, -1, PAD_A); assign(map, ACTION_COUNT, PAD_A); assign(map, GAME_A, 0);
    assert(!memcmp(&before, &map, sizeof(Map)));

    // A chord assigns just its first button.
    map = DEFAULTS;
    assign(map, GAME_A, PAD_Y | PAD_L);
    assert(map.masks[GAME_A] == PAD_Y);

    // Every action always has exactly one button, never shared.
    for (int round = 0; round < 200; ++round) {
        assign(map, round % ACTION_COUNT, BUTTONS[(round * 7) % BUTTON_COUNT]);
        uint16_t used = 0;
        for (int i = 0; i < ACTION_COUNT; ++i) {
            assert(map.masks[i] && firstButton(map.masks[i]) == map.masks[i] && !(used & map.masks[i]));
            used |= map.masks[i];
        }
    }

    // Names.
    char text[64];
    describe(DEFAULTS.masks[GAME_A], text, sizeof(text));
    assert(!strcmp(text, "A"));
    describe(0, text, sizeof(text));
    assert(!strcmp(text, "None"));
    describe(PAD_L2 | PAD_R2 | PAD_START, text, sizeof(text));
    assert(!strcmp(text, "L2, R2, Start"));
    char tiny[6];
    describe(PAD_A | PAD_B | PAD_X, tiny, sizeof(tiny));
    assert(!strcmp(tiny, "A, B"));  // stops at a whole name
    assert(!strcmp(actionName(PAUSE), "Pause menu") && !strcmp(actionName(99), ""));

    // Storage round trip; malformed, shared or old (version 1) records are refused.
    map = DEFAULTS;
    assign(map, GAME_A, PAD_R2);
    uint8_t stored[STORED_BYTES];
    store(map, stored);
    Map loaded = DEFAULTS;
    assert(restore(loaded, stored, sizeof(stored)) && !memcmp(&loaded, &map, sizeof(Map)));
    Map untouched = DEFAULTS;
    assert(!restore(untouched, stored, sizeof(stored) - 1) && isDefault(untouched));
    stored[0] = 1;  // version 1: several buttons per action
    assert(!restore(untouched, stored, sizeof(stored)) && isDefault(untouched));
    stored[0] = VERSION;
    stored[3] = stored[1]; stored[4] = stored[2];  // B on A's button: shared
    assert(!restore(untouched, stored, sizeof(stored)) && isDefault(untouched));
    store(map, stored);
    stored[1] |= 0x02;  // two buttons on one action
    assert(!restore(untouched, stored, sizeof(stored)) && isDefault(untouched));
    store(map, stored);
    stored[1] = 0; stored[2] = 0;  // an action with no button
    assert(!restore(untouched, stored, sizeof(stored)) && isDefault(untouched));

    puts("Pad mapping PASS: one-to-one defaults, swaps, pause escape, names, storage");
    return 0;
}
