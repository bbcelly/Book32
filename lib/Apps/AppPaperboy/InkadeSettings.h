#pragma once
#include "Config.h"
#if INKADE_INKBOY_VIDEO
#include <Arduino.h>
#include <vector>
#include "InkBoyDrive.h"

// Inkade display settings on the MicroSD card, in InkBoy's preset format so
// presets can be shared with InkBoy (docs/tuning.md there):
//   /inkade/settings.json                  the active settings (both modes)
//   /inkboy/presets/dithered/*.json        presets, one mode each
//   /inkboy/presets/grayscale/*.json
// Preset files: {"format":"inkboy-preset-v1","mode":"grayscale","name":"...",
// "settings":{...}}. InkBoy's voltage options (strength, weak_white_to_light)
// are specific to its SSD1681 panel and ignored here.
namespace InkadeSettings {
struct Active {
    InkBoyDrive::Settings video = InkBoyDrive::DEFAULT_SETTINGS;
    String preset[2] = {"Default", "Default"};  // per mode
    bool modified[2] = {false, false};
};
struct PresetFile { String name, path; };

bool load(Active& active);
bool save(const Active& active);
// Presets of `mode`, sorted by name (Default is built in and not listed).
std::vector<PresetFile> list(uint8_t mode);
bool loadPreset(const String& path, uint8_t mode, InkBoyDrive::Settings& video, String& name);
// Writes the current mode's settings as user-N.json; returns its name.
bool saveNewPreset(const InkBoyDrive::Settings& video, String& name, String& path);
bool overwritePreset(const String& path, const InkBoyDrive::Settings& video, const String& name);
String findPreset(uint8_t mode, const String& name);
}
#endif
