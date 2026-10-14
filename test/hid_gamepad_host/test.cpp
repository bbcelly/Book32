// Host test for the Bluetooth gamepad HID parser/decoder.
// Build: c++ -std=c++17 -I lib/Book32_Core test/hid_gamepad_host/test.cpp
#include <assert.h>
#include <stdio.h>
#include "HidGamepad.h"

using namespace HidGamepad;

int main() {
    // A typical BLE gamepad (Android / Xbox style): report 1 with X/Y/Z/Rz
    // sticks, an 8-way hat with a null state, 16 buttons; report 2 with a
    // consumer-page Home / Back key array.
    const uint8_t map[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,        // Desktop / Gamepad / Application
        0x85, 0x01,                                // Report ID 1
        0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,  // X Y Z Rz
        0x15, 0x00, 0x26, 0xFF, 0x00,              // 0..255
        0x75, 0x08, 0x95, 0x04, 0x81, 0x02,        // 4 x 8 bits, input
        0x09, 0x39, 0x15, 0x00, 0x25, 0x07,        // hat 0..7
        0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14,  // physical, units (ignored)
        0x75, 0x04, 0x95, 0x01, 0x81, 0x42,        // 4 bits, input, null state
        0x75, 0x04, 0x95, 0x01, 0x81, 0x03,        // 4 bits padding (constant)
        0x05, 0x09, 0x19, 0x01, 0x29, 0x10,        // buttons 1..16
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
        0x85, 0x02,                                // Report ID 2
        0x05, 0x0C, 0x15, 0x00, 0x26, 0xFF, 0x03,  // consumer, 0..1023
        0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,  // array
        0xC0,
    };
    Descriptor d;
    assert(d.parse(map, sizeof(map)));
    assert(d.usesReportIds);
    assert(d.count == 4 + 1 + 16 + 1);

    // Centred sticks, hat null (8), no buttons.
    uint8_t idle[] = {0x80, 0x80, 0x80, 0x80, 0x08, 0x00, 0x00};
    assert(decode(d, 1, idle, sizeof(idle)) == 0);

    // Hat up-right, button 1 (A) and 12 (Start).
    uint8_t r1[] = {0x80, 0x80, 0x80, 0x80, 0x01, 0x01, 0x08};
    uint32_t raw = 0;
    assert(decode(d, 1, r1, sizeof(r1), ANDROID_MAP, &raw) == (PAD_UP | PAD_RIGHT | PAD_A | PAD_START));
    assert(raw == ((1u << 0) | (1u << 11)));

    // Left stick hard left and down: D-pad left + down; button 2 (B).
    uint8_t r2[] = {0x00, 0xFF, 0x80, 0x80, 0x08, 0x02, 0x00};
    assert(decode(d, 1, r2, sizeof(r2)) == (PAD_LEFT | PAD_DOWN | PAD_B));

    // Buttons 4, 5, 7, 8, 11, 13: X, Y, L, R, Select, Home.
    uint8_t r3[] = {0x80, 0x80, 0x80, 0x80, 0x08, 0xD8, 0x14};
    assert(decode(d, 1, r3, sizeof(r3)) == (PAD_X | PAD_Y | PAD_L | PAD_R | PAD_SELECT | PAD_HOME));

    // Report 2: consumer AC Home (0x223) and AC Back (0x224).
    uint8_t home[] = {0x23, 0x02};
    assert(decode(d, 2, home, sizeof(home)) == PAD_HOME);
    uint8_t backKey[] = {0x24, 0x02};
    assert(decode(d, 2, backKey, sizeof(backKey)) == PAD_SELECT);
    // Report 1 fields are ignored for report 2 and vice versa.
    assert(decode(d, 2, r1, 2) == 0);

    // A pad without report IDs and with a 4-way hat (0..3) plus 8 buttons.
    const uint8_t simple[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x09, 0x39, 0x15, 0x00, 0x25, 0x03, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
        0x75, 0x04, 0x95, 0x01, 0x81, 0x01,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
        0xC0,
    };
    Descriptor s;
    assert(s.parse(simple, sizeof(simple)) && !s.usesReportIds);
    uint8_t right[] = {0x01, 0x02};  // hat 1 = right (quarters), button 2
    assert(decode(s, 0, right, sizeof(right)) == (PAD_RIGHT | PAD_B));
    uint8_t centre[] = {0x0F, 0x00};
    assert(decode(s, 0, centre, sizeof(centre)) == 0);

    // Garbage is rejected without crashing.
    const uint8_t broken[] = {0x05, 0x01, 0x27};
    Descriptor b;
    assert(!b.parse(broken, sizeof(broken)));

    puts("HID gamepad PASS: report IDs, sticks, 8- and 4-way hats, buttons, consumer keys, malformed input");
    return 0;
}
