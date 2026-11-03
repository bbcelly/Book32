#include "AppSelection.h"
#include "Config.h"
#include <Preferences.h>
#include <esp_partition.h>

namespace {
constexpr const char* PREFS = "inkdeck";
constexpr const char* KEY_APPS = "apps";
constexpr const char* KEY_INSTALL = "installId";
// The apps InkDeck had when the choice was saved. An app added later starts
// enabled once, as long as the menu is in use, instead of staying hidden.
constexpr const char* KEY_KNOWN = "appsKnown";
constexpr size_t RECORD_SECTOR = 4096;
}

AppSelection& AppSelection::getInstance() {
    static AppSelection instance;
    return instance;
}

uint8_t AppSelection::available() {
    return TODO | KLIPPER | (BOOK32_HAS_INKBOY ? INKADE : 0) | (BOOK32_HAS_TOUCH ? SUDOKU : 0);
}

void AppSelection::begin() {
    Preferences prefs;
    if (!prefs.begin(PREFS, false)) return;  // keeps ALL
    const esp_partition_t* app0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    InstallRecord record;
    if (app0 && app0->size > RECORD_SECTOR &&
        esp_partition_read(app0, app0->size - RECORD_SECTOR, &record, sizeof(record)) == ESP_OK &&
        !memcmp(record.magic, "INKAPPS1", 8) && record.version == 1 &&
        record.crc == installCrc32(reinterpret_cast<const uint8_t*>(&record), offsetof(InstallRecord, crc)) &&
        record.installId != prefs.getULong(KEY_INSTALL, 0)) {
        prefs.putUChar(KEY_APPS, record.apps & ALL);
        prefs.putULong(KEY_INSTALL, record.installId);
        prefs.putUChar(KEY_KNOWN, ALL_V1);
        Serial.printf("Apps: installer chose 0x%02X (install %08lx)\n", record.apps & ALL,
                      static_cast<unsigned long>(record.installId));
    }
    _mask = prefs.getUChar(KEY_APPS, ALL) & ALL;
    const uint8_t known = prefs.isKey(KEY_APPS) ? prefs.getUChar(KEY_KNOWN, ALL_V1) : ALL;
    if ((known & ALL) != ALL) {
        if (_mask & available()) {
            _mask |= ALL & ~known;
            prefs.putUChar(KEY_APPS, _mask);
        }
        prefs.putUChar(KEY_KNOWN, ALL);
    }
    prefs.end();
    Serial.printf("Apps: 0x%02X (%s)\n", _mask, launcher() ? "main menu" : "library home");
}

bool AppSelection::save(uint8_t mask) {
    Preferences prefs;
    if (!prefs.begin(PREFS, false)) return false;
    const bool ok = prefs.putUChar(KEY_APPS, mask & ALL) == 1;
    prefs.putUChar(KEY_KNOWN, ALL);
    prefs.end();
    return ok;
}
