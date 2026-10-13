#pragma once
#include "Config.h"
#include <string.h>

// Replace a saved sleep message that is another board's default and wrong for
// this one. Sleep settings live on the SD card, so a card moved between
// devices (or a setting saved from another device) can carry over the wrong
// wake instruction. Never replace a custom message.
inline bool sleepMessageNeedsMigration(int version, const char* message) {
    if (!message) return false;
#if defined(BOARD_LILYGO_T5S3_PRO)
    // The old LILYGO default, once; later it may be an explicit choice.
    return version < 3 && strcmp(message,"Press power to wake") == 0;
#else
    // The Sticky and Book32 wake from their power/function button, never BOOT.
    (void)version;
    return strcmp(message,"Press BOOT to wake") == 0;
#endif
}
