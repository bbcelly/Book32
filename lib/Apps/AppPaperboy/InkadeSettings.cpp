#include "InkadeSettings.h"
#if INKADE_INKBOY_VIDEO
#include <ArduinoJson.h>
#include <algorithm>
#include "Book32FS.h"

namespace InkadeSettings {
namespace {
using namespace InkBoyDrive;
constexpr const char* SETTINGS_PATH = "/inkade/settings.json";
constexpr const char* MODE_NAMES[2] = {"dithered", "grayscale"};
constexpr const char* HISTORY_NAMES[4] = {"off", "returns", "reversals", "both"};
constexpr const char* ROUTE_NAMES[ROUTES] = {"L>K>L", "D>K>D", "D>W>D", "L>W>L", "D>L>D", "L>D>L"};
constexpr char SHADES[4] = {'K', 'D', 'L', 'W'};  // InkBoy order
constexpr size_t MAX_FILE_BYTES = 8192;

String presetDir(uint8_t mode) { return String("/inkboy/presets/") + MODE_NAMES[mode == MODE_GRAYSCALE ? 1 : 0]; }

void makeDirs(const String& path) {
    // FAT needs each level created in turn.
    for (int i = 0; (i = path.indexOf('/', i + 1)) > 0;) {
        const String part = path.substring(0, i);
        if (!EbookFS.exists(part)) EbookFS.mkdir(part);
    }
    if (!EbookFS.exists(path)) EbookFS.mkdir(path);
}

void writeDithered(JsonObject o, const Settings& s) {
    o["black_pulses"] = s.blackPulses;
    o["white_pulses"] = s.whitePulses;
}
void readDithered(JsonObjectConst o, Settings& s) {
    if (o.isNull()) return;
    s.blackPulses = o["black_pulses"] | s.blackPulses;
    s.whitePulses = o["white_pulses"] | s.whitePulses;
}
void writeGray(JsonObject o, const Settings& s) {
    JsonObject doses = o.createNestedObject("doses");
    JsonObject holds = o.createNestedObject("holds");
    for (int from = 0; from < 4; ++from) {
        JsonArray row = doses.createNestedArray(String(SHADES[from]));
        JsonArray hold = holds.createNestedArray(String(SHADES[from]));
        for (int to = 0; to < 4; ++to) {
            row.add(s.gray.dose[from][to]);
            hold.add(bool(s.holds & (1u << (from * 4 + to))));
        }
    }
    o["retarget_percent"] = retargetPercent(s.gray);
    o["scan_policy"] = s.gray.scanPolicy ? "one" : "auto";
    o["strength"] = "stock";
    o["pair_hold"] = s.immediate;
    o["history"] = HISTORY_NAMES[s.adaptive.historyMode & 3];
    o["weak_white_to_light"] = "normal";
    JsonObject routes = o.createNestedObject("routes");
    for (int r = 0; r < ROUTES; ++r) {
        JsonObject route = routes.createNestedObject(ROUTE_NAMES[r]);
        route["return"] = s.adaptive.returnDelta[r];
        if (s.adaptive.reversePercent[r]) route["reverse_percent"] = s.adaptive.reversePercent[r];
        else route["reverse_percent"] = nullptr;
    }
}
void readGray(JsonObjectConst o, Settings& s) {
    if (o.isNull()) return;
    JsonObjectConst doses = o["doses"], holds = o["holds"];
    for (int from = 0; from < 4; ++from) {
        JsonArrayConst row = doses[String(SHADES[from])], hold = holds[String(SHADES[from])];
        for (int to = 0; to < 4 && row.size() == 4; ++to) s.gray.dose[from][to] = row[to] | s.gray.dose[from][to];
        for (int to = 0; to < 4 && hold.size() == 4; ++to) {
            const unsigned bit = 1u << (from * 4 + to);
            s.holds = (hold[to] | bool(s.holds & bit)) ? (s.holds | bit) : (s.holds & ~bit);
        }
    }
    const int percent = o["retarget_percent"] | int(retargetPercent(s.gray));
    if (percent >= 50 && percent <= 150 && percent % 25 == 0) s.gray.retargetGain = (percent - 50) / 25;
    s.gray.scanPolicy = !strcmp(o["scan_policy"] | (s.gray.scanPolicy ? "one" : "auto"), "one");
    s.immediate = o["pair_hold"] | s.immediate;
    const char* history = o["history"] | "";
    for (int i = 0; i < 4; ++i) if (!strcmp(history, HISTORY_NAMES[i])) s.adaptive.historyMode = i;
    JsonObjectConst routes = o["routes"];
    for (int r = 0; r < ROUTES; ++r) {
        JsonObjectConst route = routes[ROUTE_NAMES[r]];
        if (route.isNull()) continue;
        s.adaptive.returnDelta[r] = route["return"] | s.adaptive.returnDelta[r];
        s.adaptive.reversePercent[r] = route["reverse_percent"].isNull() ? 0 : route["reverse_percent"].as<uint8_t>();
    }
}

bool readJson(const String& path, DynamicJsonDocument& doc) {
    File f = EbookFS.open(path, "r");
    if (!f) return false;
    const bool ok = f.size() <= MAX_FILE_BYTES && !deserializeJson(doc, f);
    f.close();
    return ok;
}
bool writeJson(const String& path, const DynamicJsonDocument& doc) {
    makeDirs(path.substring(0, path.lastIndexOf('/')));
    const String temp = path + ".tmp";
    File f = EbookFS.open(temp, "w");
    if (!f) return false;
    const bool ok = serializeJsonPretty(doc, f) > 0;
    f.close();
    if (!ok) return false;
    if (EbookFS.exists(path)) EbookFS.remove(path);
    return EbookFS.rename(temp, path);
}
bool validName(const String& name) {
    if (name.isEmpty() || name.length() > 20 || name.equalsIgnoreCase("Default")) return false;
    for (char c : name) if (c < 32 || c > 126 || c == '"' || c == '\\') return false;
    return name[0] != ' ' && name[name.length() - 1] != ' ';
}
bool writePreset(const String& path, const Settings& video, const String& name) {
    DynamicJsonDocument doc(4096);
    doc["format"] = "inkboy-preset-v1";
    doc["mode"] = MODE_NAMES[video.mode == MODE_GRAYSCALE ? 1 : 0];
    doc["name"] = name;
    JsonObject settings = doc.createNestedObject("settings");
    if (video.mode == MODE_GRAYSCALE) writeGray(settings, video);
    else writeDithered(settings, video);
    return writeJson(path, doc);
}
}

bool load(Active& active) {
    DynamicJsonDocument doc(4096);
    // Settings from the earlier Inkade drive use another format: start fresh.
    if (!readJson(SETTINGS_PATH, doc) || strcmp(doc["format"] | "", "inkade-inkboy-v1")) return false;
    Settings& v = active.video;
    v.mode = !strcmp(doc["mode"] | "", "dithered") ? MODE_DITHERED : MODE_GRAYSCALE;
    v.showFps = doc["show_fps"] | v.showFps;
    readDithered(doc["dithered"], v);
    readGray(doc["grayscale"], v);
    for (int m = 0; m < 2; ++m) {
        JsonObjectConst p = doc["presets"][MODE_NAMES[m]];
        if (p.isNull()) continue;
        active.preset[m] = p["name"] | "Default";
        active.modified[m] = p["modified"] | false;
    }
    sanitize(v);
    return true;
}

bool save(const Active& active) {
    DynamicJsonDocument doc(4096);
    const Settings& v = active.video;
    doc["format"] = "inkade-inkboy-v1";
    doc["mode"] = MODE_NAMES[v.mode == MODE_GRAYSCALE ? 1 : 0];
    doc["show_fps"] = v.showFps;
    writeDithered(doc.createNestedObject("dithered"), v);
    writeGray(doc.createNestedObject("grayscale"), v);
    JsonObject presets = doc.createNestedObject("presets");
    for (int m = 0; m < 2; ++m) {
        JsonObject p = presets.createNestedObject(MODE_NAMES[m]);
        p["name"] = active.preset[m];
        p["modified"] = active.modified[m];
    }
    return writeJson(SETTINGS_PATH, doc);
}

std::vector<PresetFile> list(uint8_t mode) {
    std::vector<PresetFile> files;
    File dir = EbookFS.open(presetDir(mode));
    if (!dir || !dir.isDirectory()) return files;
    for (File f = dir.openNextFile(); f && files.size() < 16; f = dir.openNextFile()) {
        String path = f.name();
        if (!path.startsWith("/")) path = presetDir(mode) + "/" + path;
        const bool json = !f.isDirectory() && path.endsWith(".json");
        f.close();
        if (!json) continue;
        Settings probe = DEFAULT_SETTINGS;
        String name;
        if (loadPreset(path, mode, probe, name)) files.push_back({name, path});
    }
    dir.close();
    std::sort(files.begin(), files.end(), [](const PresetFile& a, const PresetFile& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return files;
}

bool loadPreset(const String& path, uint8_t mode, Settings& video, String& name) {
    DynamicJsonDocument doc(4096);
    if (!readJson(path, doc)) return false;
    if (strcmp(doc["format"] | "", "inkboy-preset-v1") ||
        strcmp(doc["mode"] | "", MODE_NAMES[mode == MODE_GRAYSCALE ? 1 : 0]))
        return false;
    const String candidate = doc["name"] | "";
    if (!validName(candidate)) return false;
    if (mode == MODE_GRAYSCALE) readGray(doc["settings"], video);
    else readDithered(doc["settings"], video);
    sanitize(video);
    name = candidate;
    return true;
}

bool saveNewPreset(const Settings& video, String& name, String& path) {
    const String dir = presetDir(video.mode);
    for (int n = 1; n < 100; ++n) {
        path = dir + "/user-" + String(n) + ".json";
        if (EbookFS.exists(path)) continue;
        name = "User " + String(n);
        return writePreset(path, video, name);
    }
    return false;
}

bool overwritePreset(const String& path, const Settings& video, const String& name) {
    const String backup = path + ".bak";
    if (EbookFS.exists(backup)) EbookFS.remove(backup);
    if (EbookFS.exists(path)) {
        File in = EbookFS.open(path, "r"), out = EbookFS.open(backup, "w");
        uint8_t buffer[256];
        while (in && out && in.available()) out.write(buffer, in.read(buffer, sizeof(buffer)));
        if (in) in.close();
        if (out) out.close();
    }
    return writePreset(path, video, name);
}

String findPreset(uint8_t mode, const String& name) {
    for (const PresetFile& p : list(mode)) if (p.name == name) return p.path;
    return "";
}
}
#endif
