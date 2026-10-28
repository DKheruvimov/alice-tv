#include "transport/adb/AdbTransport.h"
#include <vector>

// Логические имена -> имена пакетов. Первичные значения проверены на Xiaomi Mi TV 32 P1
// через инвентарь (2026-10-27). Пакеты нужно сверять на каждом целевом ТВ (AGENTS.md 4.2).
struct AppAlias {
    const char* name;
    const char* pkg;
};

static const AppAlias APP_ALIASES[] = {
    {"kinopoisk", "ru.kinopoisk.tv"},
    {"youtube",   "com.google.android.youtube.tv"},
    {"vk",        "com.vk.tv"},
    {"vkvideo",   "com.vk.tv"},
    {"rutube",    "ru.rutube.app"},
    {"ivi",       "ru.ivi.client"},
    {"vlc",       "org.videolan.vlc"},
    {"nova",      "org.courville.nova"},
    {"playstore", "com.android.vending"},
};

// Символические имена клавиш -> Android keycode (соответствие контракту key, AGENTS.md 4.2).
struct KeyAlias {
    const char* name;
    int code;
};

static const KeyAlias KEY_ALIASES[] = {
    {"power", 26}, {"home", 3}, {"back", 4}, {"menu", 82}, {"info", 165},
    {"up", 19}, {"down", 20}, {"left", 21}, {"right", 22}, {"ok", 23}, {"enter", 23}, {"select", 23},
    {"vol_up", 24}, {"vol_down", 25}, {"mute", 164},
    {"play_pause", 85}, {"play", 126}, {"pause", 127}, {"stop", 86},
    {"media_next", 87}, {"media_prev", 88}, {"rewind", 89}, {"forward", 90},
    {"red", 184}, {"green", 185}, {"yellow", 186}, {"blue", 187},
};

static const char* FAILURE_MARKERS[] = {
    "Error type", "INSTRUMENTATION_FAILED", "No matches found",
    "Exception", "do not exist", "Permission Denial", "Reason: Activity class",
};

static bool hasFailure(const String& out) {
    for (const char* m : FAILURE_MARKERS) {
        if (out.indexOf(m) >= 0) return true;
    }
    return false;
}

static String compact(const String& s, size_t maxLen = 300) {
    String o = s;
    o.replace('\n', ' ');
    o.replace('\r', ' ');
    while (o.indexOf("  ") >= 0) o.replace("  ", " ");
    if (o.length() > maxLen) o = o.substring(0, maxLen) + "...";
    return o;
}

bool AdbTransport::begin(IPAddress ip, uint16_t port) {
    return client.begin(ip, port);
}

bool AdbTransport::isConnected() {
    return client.isConnected();
}

String AdbTransport::resolvePackage(const String& target) {
    // Уже похоже на имя пакета (содержит точку) — используем как есть.
    if (target.indexOf('.') >= 0) return target;
    for (const AppAlias& a : APP_ALIASES) {
        if (target.equalsIgnoreCase(a.name)) return a.pkg;
    }
    return "";
}

TvResult AdbTransport::launch(const String& target) {
    TvResult res;
    String pkg = resolvePackage(target);
    if (pkg.length() == 0) {
        res.detail = "unknown app: " + target;
        return res;
    }
    // "monkey -p <pkg> 1" универсально запускает главную активность пакета
    // как для стандартных LAUNCHER, так и для ТВ LEANBACK_LAUNCHER (Кинопоиск, VK Video, Иви и др.)
    String cmd = "monkey -p " + pkg + " 1";
    AdbShellResult r = client.execShell(cmd, 6000);
    res.ok = r.ok && !hasFailure(r.output);
    res.detail = (r.output.length() > 0 ? compact(r.output) : (res.ok ? "launched " + pkg : "launch failed"));
    Serial.printf("[LAUNCH] %s -> %s (%s)\n", target.c_str(), pkg.c_str(), res.ok ? "OK" : "FAIL");
    return res;
}

TvResult AdbTransport::search(const String& query) {
    TvResult res;
    if (query.length() == 0) {
        res.detail = "empty query";
        return res;
    }
    String escaped = query;
    escaped.replace("\"", "\\\"");
    escaped.replace("'", "'\\''");
    // Глобальный голосовой/текстовый поиск Android TV (эмуляция "Алиса, найди ...")
    String cmd = "am start -a android.search.action.GLOBAL_SEARCH --es query '" + escaped + "'";
    AdbShellResult r = client.execShell(cmd, 6000);
    res.ok = r.ok && !hasFailure(r.output);
    res.detail = res.ok ? ("searching: " + query) : compact(r.output);
    Serial.printf("[SEARCH] '%s' (%s)\n", query.c_str(), res.ok ? "OK" : "FAIL");
    return res;
}

TvResult AdbTransport::key(const String& name) {
    TvResult res;
    int code = -1;
    if (name.length() > 0 && name.charAt(0) >= '0' && name.charAt(0) <= '9') {
        code = name.toInt(); // разрешаем числовой keycode
    } else {
        for (const KeyAlias& k : KEY_ALIASES) {
            if (name.equalsIgnoreCase(k.name)) { code = k.code; break; }
        }
    }
    if (code < 0) {
        res.detail = "unknown key: " + name;
        return res;
    }
    String cmd = "input keyevent " + String(code);
    AdbShellResult r = client.execShell(cmd, 5000);
    res.ok = r.ok && !hasFailure(r.output);
    res.detail = res.ok ? ("key " + name + " (" + String(code) + ")") : compact(r.output);
    return res;
}

TvResult AdbTransport::text(const String& value) {
    TvResult res;
    // input text не переносит пробелы/спецсимволы напрямую: пробел -> %s, остальное фильтруем.
    String sanitized;
    for (size_t i = 0; i < value.length(); i++) {
        char c = value.charAt(i);
        if (c == ' ') sanitized += "%s";
        else if (isalnum(c) || c == '.' || c == ',' || c == '@' || c == '-' || c == '_' || c == ':' || c == '/') sanitized += c;
        else if (c >= 0x80) sanitized += c; // UTF-8 (кириллица) — передать как есть, поддержка зависит от IME
    }
    String cmd = "input text \"" + sanitized + "\"";
    AdbShellResult r = client.execShell(cmd, 6000);
    res.ok = r.ok && !hasFailure(r.output);
    res.detail = res.ok ? "text sent" : compact(r.output);
    return res;
}

TvResult AdbTransport::seek(int steps) {
    TvResult res;
    if (steps == 0) { res.ok = true; res.detail = "no-op"; return res; }
    int code = (steps > 0) ? 90 /*forward*/ : 89 /*rewind*/;
    int n = abs(steps);
    if (n > 20) n = 20; // защита от зависания основного цикла
    for (int i = 0; i < n; i++) {
        client.execShell("input keyevent " + String(code), 3000);
        delay(80);
    }
    res.ok = true;
    res.detail = String(steps > 0 ? "ffwd " : "rew ") + String(n);
    return res;
}

String AdbTransport::listApps() {
    // Только реально запускаемые пакеты: извлекаем packageName, дедуплицируем на устройстве
    // (grep -oE ... | sort -u), чтобы вывод не упирался в лимит буфера.
    AdbShellResult r = client.execShell(
        "cmd package query-activities -a android.intent.action.MAIN -c android.intent.category.LAUNCHER | grep -oE 'packageName=[^ ]+' | sort -u",
        12000);

    String json = "[";
    std::vector<String> seen;
    int idx = 0;
    while ((idx = r.output.indexOf("packageName=", idx)) >= 0) {
        idx += strlen("packageName=");
        int nl = r.output.indexOf('\n', idx);
        if (nl < 0) nl = r.output.length();
        String pkg = r.output.substring(idx, nl);
        pkg.trim();
        pkg.replace("\r", "");
        bool dup = false;
        for (const String& s : seen) if (s == pkg) { dup = true; break; }
        if (!dup && pkg.length() > 0) {
            seen.push_back(pkg);
            if (json.length() > 1) json += ",";
            json += "\"" + pkg + "\"";
        }
        idx = nl;
    }
    json += "]";
    return json;
}

AdbShellResult AdbTransport::rawShell(const String& cmd, uint32_t timeoutMs) {
    return client.execShell(cmd, timeoutMs);
}
