#ifndef UNIT_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config/Config.h"
#include "transport/ITvTransport.h"
#include "transport/adb/AdbTransport.h"
#include "transport/remotev2/RemoteV2Transport.h"
#include "transport/remotev2/crypto/DeviceIdentity.h"
#include "transport/WakeupHelper.h"

// ==========================================
// ГЛОБАЛЬНЫЕ КОМПОНЕНТЫ
// ==========================================
// Транспорт выбирается из Config (NVS): "adb" | "remote_v2".
AdbTransport adb;
RemoteV2Transport remoteV2;
ITvTransport* tv = &adb;

WebServer server(80);

static const char* AP_SSID = "Alice-TV-Setup";
static uint32_t bootAt = 0;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ESP32 Xiaomi Mi TV Remote</title>
  <style>
    :root {
      --bg: #090d16;
      --card-bg: rgba(22, 30, 49, 0.75);
      --card-border: rgba(255, 255, 255, 0.08);
      --primary: #3b82f6;
      --primary-glow: rgba(59, 130, 246, 0.35);
      --danger: #ef4444;
      --danger-glow: rgba(239, 68, 68, 0.4);
      --success: #10b981;
      --text: #f8fafc;
      --text-muted: #94a3b8;
      --btn-bg: #1e293b;
      --btn-hover: #334155;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; user-select: none; -webkit-tap-highlight-color: transparent; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: var(--bg);
      background-image: radial-gradient(circle at 50% 0%, #1e1b4b 0%, var(--bg) 75%);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 16px;
    }
    .container { width: 100%; max-width: 440px; display: flex; flex-direction: column; gap: 14px; }
    header { text-align: center; padding: 6px 0; }
    header h1 { font-size: 20px; font-weight: 700; letter-spacing: -0.5px; }
    .status-badge {
      display: inline-flex; align-items: center; gap: 6px; font-size: 12px;
      color: var(--text-muted); background: var(--card-bg); border: 1px solid var(--card-border);
      padding: 4px 12px; border-radius: 999px; margin-top: 6px;
    }
    .status-dot { width: 8px; height: 8px; background: var(--success); border-radius: 50%; box-shadow: 0 0 8px var(--success); }
    .card {
      background: var(--card-bg); border: 1px solid var(--card-border);
      backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px);
      border-radius: 20px; padding: 18px; box-shadow: 0 10px 30px rgba(0,0,0,0.5);
    }
    .card-title {
      font-size: 12px; font-weight: 600; text-transform: uppercase; letter-spacing: 1px;
      color: var(--text-muted); margin-bottom: 14px; text-align: center;
    }
    .btn {
      background: var(--btn-bg); color: var(--text); border: 1px solid rgba(255,255,255,0.06);
      border-radius: 14px; font-size: 15px; font-weight: 600; cursor: pointer;
      display: inline-flex; align-items: center; justify-content: center;
      transition: all 0.1s ease; touch-action: manipulation; box-shadow: 0 4px 12px rgba(0,0,0,0.2);
    }
    .btn:active { transform: scale(0.94); background: var(--btn-hover); }
    .btn-power {
      background: linear-gradient(135deg, #ef4444, #b91c1c); width: 62px; height: 62px;
      border-radius: 50%; font-size: 24px; box-shadow: 0 0 20px var(--danger-glow);
    }
    .btn-power:active { transform: scale(0.9); }
    .row { display: flex; gap: 10px; justify-content: center; align-items: center; }
    .vol-row { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; margin: 12px 0; }
    .vol-btn { height: 48px; }
    .dpad-container {
      display: grid; grid-template-columns: repeat(3, 70px); grid-template-rows: repeat(3, 70px);
      gap: 8px; justify-content: center; margin: 10px auto;
    }
    .dpad-btn { border-radius: 16px; font-size: 18px; }
    .dpad-center {
      background: linear-gradient(135deg, var(--primary), #1d4ed8);
      font-size: 16px; font-weight: 700; box-shadow: 0 0 16px var(--primary-glow);
    }
    .apps-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .app-btn {
      height: 46px; font-size: 14px; gap: 8px; justify-content: flex-start; padding-left: 14px;
    }
    .input-group { display: flex; gap: 8px; }
    .input-field {
      flex: 1; background: #0f172a; border: 1px solid var(--card-border);
      border-radius: 12px; padding: 10px 14px; color: var(--text); font-size: 13px; font-family: monospace;
      outline: none;
    }
    .input-field:focus { border-color: var(--primary); }
    .chip {
      background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1);
      border-radius: 999px; padding: 6px 12px; font-size: 12px; cursor: pointer;
      color: var(--text-muted); transition: all 0.15s ease;
    }
    .chip:hover, .chip:active { background: var(--primary); color: #fff; border-color: var(--primary); }
    .log-box {
      background: #020617; border-radius: 10px; padding: 10px; font-family: monospace;
      font-size: 11px; color: #38bdf8; min-height: 48px; max-height: 80px; overflow-y: auto;
      border: 1px solid rgba(255,255,255,0.05);
    }
    .link { color: var(--text-muted); font-size: 11px; text-align: center; text-decoration: none; }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>📺 Xiaomi Mi TV Remote</h1>
      <div class="status-badge">
        <span class="status-dot"></span>
        <span id="status-text">ESP32</span>
      </div>
    </header>

    <!-- Главный пульт -->
    <div class="card">
      <div class="row" style="margin-bottom: 12px;">
        <button class="btn btn-power" onclick="sendCmd('power')">⏻</button>
      </div>

      <div class="vol-row">
        <button class="btn vol-btn" onclick="sendCmd('vol_down')">VOL −</button>
        <button class="btn vol-btn" onclick="sendCmd('mute')">MUTE</button>
        <button class="btn vol-btn" onclick="sendCmd('vol_up')">VOL +</button>
      </div>

      <!-- Джойстик -->
      <div class="dpad-container">
        <div></div>
        <button class="btn dpad-btn" onclick="sendCmd('up')">▲</button>
        <div></div>
        <button class="btn dpad-btn" onclick="sendCmd('left')">◀</button>
        <button class="btn dpad-btn dpad-center" onclick="sendCmd('ok')">OK</button>
        <button class="btn dpad-btn" onclick="sendCmd('right')">▶</button>
        <div></div>
        <button class="btn dpad-btn" onclick="sendCmd('down')">▼</button>
        <div></div>
      </div>

      <div class="vol-row" style="margin-bottom: 0;">
        <button class="btn vol-btn" onclick="sendCmd('back')">↩ НАЗАД</button>
        <button class="btn vol-btn" onclick="sendCmd('home')">⌂ ДОМОЙ</button>
        <button class="btn vol-btn" onclick="sendCmd('settings')">⚙ НАСТРОЙКИ</button>
      </div>
    </div>

    <!-- Голосовой / текстовый поиск (эмуляция Алисы) -->
    <div class="card">
      <div class="card-title">🔍 Поиск на ТВ (эмуляция Алисы)</div>
      <div class="input-group">
        <input type="text" id="searchInput" class="input-field" placeholder="например: фильм Аватар" value="фильм Аватар">
        <button class="btn" style="padding: 0 16px; background: linear-gradient(135deg, #8b5cf6, #6366f1); color: #fff;" onclick="sendSearch()">Найти</button>
      </div>
      <div style="display: flex; gap: 6px; margin-top: 10px; flex-wrap: wrap;">
        <span class="chip" onclick="quickSearch('фильм Аватар')">🎬 фильм Аватар</span>
        <span class="chip" onclick="quickSearch('Аниме Аватар')">✨ Аниме Аватар</span>
        <span class="chip" onclick="quickSearch('новости')">📰 новости</span>
        <span class="chip" onclick="quickSearch('музыка')">🎵 музыка</span>
      </div>
    </div>

    <!-- Быстрый запуск приложений -->
    <div class="card">
      <div class="card-title">Быстрый запуск приложений</div>
      <div class="apps-grid">
        <button class="btn app-btn" onclick="sendCmd('kinopoisk')">🎬 Кинопоиск</button>
        <button class="btn app-btn" onclick="sendCmd('vkvideo')">📱 VK Видео</button>
        <button class="btn app-btn" onclick="sendCmd('rutube')">📺 Rutube</button>
        <button class="btn app-btn" onclick="sendCmd('ivi')">🍿 Иви</button>
        <button class="btn app-btn" onclick="sendCmd('vlc')">🎬 VLC</button>
        <button class="btn app-btn" onclick="sendCmd('youtube')">▶ YouTube</button>
      </div>
      <div class="input-group" style="margin-top: 12px;">
        <button class="btn" style="flex:1; height: 40px; font-size: 13px;" onclick="loadApps()">🔄 Все приложения ТВ</button>
      </div>
      <div id="appsBox" class="apps-grid" style="margin-top: 10px;"></div>
      <div class="input-group" style="margin-top: 10px;">
        <input type="text" id="pkgInput" class="input-field" placeholder="имя пакета, напр. com.foo.app">
        <button class="btn" style="padding: 0 14px;" onclick="launchPackage()">Открыть</button>
      </div>
    </div>

    <!-- Тестовая строка ввода -->
    <div class="card" id="adbCard">
      <div class="card-title">ADB Shell Консоль</div>
      <div class="input-group">
        <input type="text" id="cmdInput" class="input-field" placeholder="input keyevent 24" value="input keyevent 24">
        <button class="btn" style="padding: 0 16px;" onclick="sendCustomShell()">Пуск</button>
      </div>
      <div class="input-group" style="margin-top: 8px;">
        <button class="btn" style="flex:1; height: 40px; font-size: 13px;" onclick="fetchPackages()">📋 Список пакетов ТВ</button>
      </div>
      <div id="logBox" class="log-box" style="margin-top: 10px; max-height: 180px;">> Готов к отправке команд...</div>
    </div>

    <a class="link" href="/setup">Настройки устройства (Wi-Fi / телевизор / транспорт)</a>
  </div>

  <script>
    function log(msg) {
      const b = document.getElementById('logBox');
      const time = new Date().toLocaleTimeString();
      const text = String(msg).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/\n/g, '<br>');
      b.innerHTML = `[${time}] ${text}<br>` + b.innerHTML;
    }

    function haptic() {
      if (navigator.vibrate) navigator.vibrate(30);
    }

    async function sendCmd(action) {
      haptic();
      log(`Отправка команды: ${action}...`);
      try {
        const resp = await fetch(`/api/cmd?action=${encodeURIComponent(action)}`);
        const res = await resp.text();
        log(`Ответ: ${res}`);
      } catch (err) {
        log(`Ошибка: ${err}`);
      }
    }

    async function sendSearch() {
      haptic();
      const input = document.getElementById('searchInput');
      const q = input.value.trim();
      if (!q) return;
      log(`Поиск на ТВ: ${q}...`);
      try {
        const resp = await fetch(`/api/search?q=${encodeURIComponent(q)}`);
        const res = await resp.text();
        log(`Ответ поиска: ${res}`);
      } catch (err) {
        log(`Ошибка: ${err}`);
      }
    }

    function quickSearch(text) {
      document.getElementById('searchInput').value = text;
      sendSearch();
    }

    async function sendCustomShell() {
      haptic();
      const input = document.getElementById('cmdInput').value.trim();
      if (!input) return;
      log(`Shell: ${input}`);
      try {
        const resp = await fetch(`/api/shell?cmd=${encodeURIComponent(input)}`);
        const res = await resp.text();
        log(`Ответ: ${res}`);
      } catch (err) {
        log(`Ошибка: ${err}`);
      }
    }

    async function fetchPackages() {
      haptic();
      log('Запрос списка пакетов...');
      try {
        const resp = await fetch('/api/packages');
        const res = await resp.text();
        log(res);
      } catch (err) {
        log(`Ошибка: ${err}`);
      }
    }

    // Автообнаружение: ТВ сам отдаёт список запускаемых пакетов, ярлыки создаются без рефлеша.
    async function loadApps() {
      haptic();
      const box = document.getElementById('appsBox');
      box.innerHTML = '<div style="color:var(--text-muted);font-size:12px;">Сканирование ТВ...</div>';
      try {
        const resp = await fetch('/api/apps');
        const list = await resp.json();
        if (!list.length) { box.innerHTML = ''; log('Приложения не найдены'); return; }
        box.innerHTML = list.map(pkg => {
          const short = pkg.split('.').filter(s => !['com','org','ru','tv','app','android','google','mobile'].includes(s.toLowerCase())).pop() || pkg;
          return `<button class="btn app-btn" onclick="sendCmd('${pkg}')" title="${pkg}">▸ ${short}</button>`;
        }).join('');
        log(`Найдено приложений: ${list.length}`);
      } catch (err) {
        box.innerHTML = '';
        log(`Ошибка загрузки приложений: ${err}`);
      }
    }

    function launchPackage() {
      const v = document.getElementById('pkgInput').value.trim();
      if (v) sendCmd(v);
    }

    async function refreshStatus() {
      try {
        const r = await fetch('/api/status');
        const s = await r.json();
        document.getElementById('status-text').innerHTML =
          `ESP32 &bull; ${s.ip} &bull; ${s.transport} &bull; ${s.tv_connected ? 'ТВ: OK' : 'ТВ: нет'}`;
        // ADB-консоль имеет смысл только на ADB-транспорте.
        const card = document.getElementById('adbCard');
        if (card && s.transport !== 'adb') card.style.display = 'none';
      } catch (e) {}
    }
    refreshStatus();
    setInterval(refreshStatus, 5000);
  </script>
</body>
</html>
)rawliteral";

const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="ru"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Alice TV - настройки</title>
<style>
 body{font-family:sans-serif;background:#090d16;color:#f8fafc;padding:20px;max-width:520px;margin:auto}
 label{display:block;margin-top:12px;font-size:13px;color:#94a3b8}
 input,select{width:100%;padding:10px;margin-top:4px;border-radius:8px;border:1px solid #334155;background:#0f172a;color:#f8fafc}
 button{margin-top:16px;padding:12px 16px;border:0;border-radius:10px;background:#3b82f6;color:#fff;font-weight:700;cursor:pointer;width:100%}
 .muted{color:#94a3b8;font-size:12px}
</style></head><body>
<h2>Настройки устройства</h2>
<p class="muted">Пароли хранятся только в NVS устройства, не в репозитории (AGENTS 6.3).</p>
<form method="POST" action="/setup">
 <label>Wi-Fi SSID</label><input name="ssid" placeholder="имя сети">
 <label>Wi-Fi пароль</label><input name="pass" placeholder="пароль сети">
 <label>IP телевизора</label><input name="tv_ip" placeholder="192.168.0.26">
 <label>MAC-адрес телевизора (для WoWLAN)</label><input name="tv_mac" placeholder="18:ef:3a:89:2b:c9">
 <label>Порт</label><input name="tv_port" type="number" placeholder="5555 / 6466">
 <label>Транспорт</label>
 <select name="transport"><option value="adb">ADB (MVP)</option><option value="remote_v2">Google TV Remote v2</option></select>
 <button type="submit">Сохранить и перезагрузить</button>
</form>
<p><a href="/" style="color:#94a3b8">← к пульту</a></p>
</body></html>
)rawliteral";

void handleRoot() {
    server.send(200, "text/html", INDEX_HTML);
}

// Список символических имён клавиш (иначе /api/cmd трактуется как launch).
static bool isKeyName(const String& a) {
    static const char* names[] = {
        "power","home","back","menu","info","up","down","left","right","ok","enter","select",
        "vol_up","vol_down","mute","play_pause","play","pause","stop",
        "media_next","media_prev","rewind","forward","red","green","yellow","blue"
    };
    for (const char* n : names) if (a == n) return true;
    return a.length() > 0 && a.charAt(0) >= '0' && a.charAt(0) <= '9'; // числовой keycode
}

static String htmlEscape(const String &in) {
    String out = in;
    out.replace("&", "&amp;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    out.replace("\"", "&quot;");
    return out;
}

// Единая точка входа команд. action = имя клавиши | псевдоним приложения | имя пакета | "settings".
void handleCmd() {
    if (!server.hasArg("action")) {
        server.send(400, "text/plain", "Missing action");
        return;
    }
    String act = server.arg("action");
    Serial.printf("[WEB] Команда: %s\n", act.c_str());

    TvResult res;
    if (act == "power" || act == "wake") {
        res = WakeupHelper::wake(tv, config.tvIp, config.tvPort, config.tvMac);
    } else if (act == "settings") {
        if (String(tv->serviceType()) == "adb") {
            AdbShellResult r = adb.rawShell("am start -a android.settings.SETTINGS");
            res.ok = r.ok;
            res.detail = r.output;
        } else {
            res.detail = "settings: not supported on this transport yet";
        }
    } else if (act == "search") {
        String q = server.hasArg("q") ? server.arg("q") : (server.hasArg("target") ? server.arg("target") : "");
        res = tv->search(q);
    } else if (isKeyName(act)) {
        res = tv->key(act);
    } else {
        res = tv->launch(act); // псевдоним ИЛИ произвольный пакет без рефлеша
    }

    String resp = String(res.ok ? "OK" : "ERROR") + (res.detail.length() ? (": " + res.detail) : "");
    server.send(200, "text/html", htmlEscape(resp));
}

void handleSearch() {
    String q;
    if (server.hasArg("q")) q = server.arg("q");
    else if (server.hasArg("query")) q = server.arg("query");
    else {
        server.send(400, "text/plain", "Missing q");
        return;
    }
    Serial.printf("[WEB] Search: %s\n", q.c_str());
    TvResult res = tv->search(q);
    String resp = String(res.ok ? "OK" : "ERROR") + (res.detail.length() ? (": " + res.detail) : "");
    server.send(200, "text/html", htmlEscape(resp));
}

void handleWake() {
    Serial.println("[WEB] Вызов /api/wake");
    TvResult res = WakeupHelper::wake(tv, config.tvIp, config.tvPort, config.tvMac);
    String resp = String(res.ok ? "OK" : "ERROR") + (res.detail.length() ? (": " + res.detail) : "");
    server.send(200, "text/html", htmlEscape(resp));
}

void handleShell() {
    if (String(tv->serviceType()) != "adb") {
        server.send(400, "text/plain", "shell: доступно только на ADB-транспорте");
        return;
    }
    if (!server.hasArg("cmd")) {
        server.send(400, "text/plain", "Missing cmd");
        return;
    }
    String cmd = server.arg("cmd");
    Serial.printf("[WEB] Shell: %s\n", cmd.c_str());
    AdbShellResult r = adb.rawShell(cmd, 10000);
    String resp = r.output.length() > 0 ? r.output : (r.ok ? "OK" : "ERROR");
    server.send(200, "text/html", htmlEscape(resp));
}

// Инвентарь реально запускаемых приложений (то, что имеет LAUNCHER-активность).
void handleApps() {
    String json = tv->listApps();
    server.send(200, "application/json", json);
}

// Сырой список сторонних пакетов (для отладки/поиска имён). Только ADB.
void handlePackages() {
    if (String(tv->serviceType()) != "adb") {
        server.send(400, "text/plain", "packages: доступно только на ADB-транспорте");
        return;
    }
    AdbShellResult r = adb.rawShell("pm list packages -3", 12000);
    if (!r.ok && r.output.length() == 0) {
        server.send(500, "text/plain", "ADB error: TV недоступен");
        return;
    }
    server.send(200, "text/html", htmlEscape(r.output));
}

void handleKey() {
    if (!server.hasArg("code")) {
        server.send(400, "text/plain", "Missing code");
        return;
    }
    int code = server.arg("code").toInt();
    Serial.printf("[WEB] Keycode: %d\n", code);
    TvResult res = tv->key(String(code));
    server.send(200, "text/plain", res.ok ? "OK" : "ERROR");
}

void handleStatus() {
    String json = "{";
    json += "\"ip\": \"" + WiFi.localIP().toString() + "\",";
    json += "\"tv_ip\": \"" + config.tvIp.toString() + "\",";
    json += "\"tv_mac\": \"" + config.tvMac + "\",";
    json += "\"transport\": \"" + String(tv->serviceType()) + "\",";
    json += "\"rssi\": " + String(WiFi.RSSI()) + ",";
    json += "\"tv_connected\": " + String(tv->isConnected() ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
}

// Временный дебаг для проверки P1b: генерация/персист идентичности на устройстве.
// GET /api/debug/identity        — гарантировать идентичность, вернуть размеры DER и время.
// GET /api/debug/identity?wipe=1 — стереть и перегенерировать (проверка пути генерации).
void handleDebugIdentity() {
    bool wiped = server.hasArg("wipe");
    unsigned long t0 = millis();
    if (wiped) deviceIdentity.wipe();
    bool hadBefore = deviceIdentity.isReady();
    bool ok = deviceIdentity.ensure("Alice TV Remote");
    unsigned long dt = millis() - t0;
    String json = "{";
    json += "\"ok\": " + String(ok ? "true" : "false") + ",";
    json += "\"was_ready_before\": " + String(hadBefore && !wiped ? "true" : "false") + ",";
    json += "\"elapsed_ms\": " + String(dt) + ",";
    json += "\"cert_len\": " + String(deviceIdentity.certLen()) + ",";
    json += "\"key_len\": " + String(deviceIdentity.keyLen());
    // ?hex=1 — временно вернуть cert DER как hex для офлайн-валидации X.509.
    if (server.hasArg("hex") && deviceIdentity.isReady()) {
        const uint8_t* c = deviceIdentity.certDer();
        uint32_t n = deviceIdentity.certLen();
        String hex;
        hex.reserve(n * 2);
        for (uint32_t i = 0; i < n; i++) { char b[3]; snprintf(b, sizeof(b), "%02x", c[i]); hex += b; }
        json += ",\"cert_hex\": \"" + hex + "\"";
    }
    json += "}";
    Serial.printf("[DBG] identity ok=%d elapsed=%lums cert=%u key=%u\n", ok, dt, deviceIdentity.certLen(), deviceIdentity.keyLen());
    server.send(200, "application/json", json);
}

void handleSetupForm() {
    server.send(200, "text/html", SETUP_HTML);
}

void handleSetupSave() {
    if (server.hasArg("ssid")) config.setWifi(server.arg("ssid"), server.arg("pass"));
    if (server.hasArg("tv_ip")) {
        IPAddress ip;
        if (ip.fromString(server.arg("tv_ip"))) config.setTv(ip, (uint16_t)server.arg("tv_port").toInt());
    }
    if (server.hasArg("tv_port")) config.tvPort = (uint16_t)server.arg("tv_port").toInt();
    if (server.hasArg("tv_mac")) config.tvMac = server.arg("tv_mac");
    if (server.hasArg("transport")) config.setTransport(server.arg("transport"));
    bool ok = config.save();
    server.send(200, "text/html",
        ok ? "<h3>Сохранено. Перезагрузка...</h3><script>setTimeout(function(){location='/';},3000)</script>"
           : "<h3>Ошибка записи NVS</h3><a href='/setup'>Назад</a>");
    if (ok) {
        delay(500);
        ESP.restart();
    }
}

// Фоновая генерация идентичности v2 (RSA-2048 ~100-200с на C3) — вне loop() (AGENTS 6.1).
static volatile bool gIdentityBusy = false;
static void identityTask(void*) {
    gIdentityBusy = true;
    bool ok = deviceIdentity.ensure("Alice TV Remote");
    Serial.printf("[ID] фоновая генерация идентичности: %s (cert=%u key=%u)\n",
                  ok ? "OK" : "FAIL", deviceIdentity.certLen(), deviceIdentity.keyLen());
    gIdentityBusy = false;
    vTaskDelete(NULL);
}

// Неблокирующее обслуживание транспорта в loop(). Для ADB — no-op (соединение
// устанавливается по требованию в execShell); для v2 — крутим стейт-машину
// pairing/control поверх общего TLS-синглтона.
void tvTick() {
    if (tv == &remoteV2) remoteV2.tick();
}

// ---- Google TV Remote v2: сопряжение (PIN) через веб-UI (P3) ----
void handlePairingStatus() {
    bool isV2 = (tv == &remoteV2);
    String json = "{";
    json += "\"transport\": \"" + String(tv->serviceType()) + "\",";
    json += "\"identity_ready\": " + String(deviceIdentity.isReady() ? "true" : "false") + ",";
    json += "\"identity_busy\": " + String(gIdentityBusy ? "true" : "false") + ",";
    json += "\"v2\": " + String(isV2 ? "true" : "false") + ",";
    json += "\"paired\": " + String((isV2 && remoteV2.isPaired()) ? "true" : "false") + ",";
    json += "\"control_ready\": " + String((isV2 && remoteV2.isReady()) ? "true" : "false") + ",";
    json += "\"awaiting_pin\": " + String((isV2 && remoteV2.pairingAwaitingPin()) ? "true" : "false") + ",";
    json += "\"current_app\": \"" + htmlEscape(isV2 ? remoteV2.currentApp() : String("")) + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

void handlePairingStart() {
    if (tv != &remoteV2) { server.send(400, "text/plain", "транспорт не remote_v2"); return; }
    if (!deviceIdentity.isReady()) {
        server.send(409, "text/plain", "идентичность ещё генерируется в фоне; подождите завершения и повторите");
        return;
    }
    bool ok = remoteV2.startPairing("Alice TV");
    server.send(200, "text/html", ok ? "pairing started" : "уже сопряжение идёт");
}

void handlePairingPin() {
    if (tv != &remoteV2) { server.send(400, "text/plain", "транспорт не remote_v2"); return; }
    if (!server.hasArg("pin")) { server.send(400, "text/plain", "Missing pin"); return; }
    bool ok = remoteV2.submitPin(server.arg("pin"));
    server.send(200, "text/html", htmlEscape(ok ? "pin accepted" : "pin rejected"));
}

void handlePairingPage() {
    const char* page = R"rawliteral(<!DOCTYPE html><html lang=ru><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Сопряжение v2</title>
<style>body{font-family:system-ui;background:#090d16;color:#f8fafc;padding:16px}
.card{background:#16203a;border-radius:12px;padding:16px;max-width:420px;margin:auto}
input,button{font-size:18px;padding:10px;margin:6px 0;width:100%}
#st{white-space:pre-wrap;font-size:14px;color:#94a3b8}</style></head>
<body><div class=card><h3>Сопряжение Google TV (PIN)</h3>
<div id=st>загрузка…</div>
<button onclick=pair()>Начать сопряжение</button>
<input id=pin inputmode=text maxlength=6 placeholder="6 hex-символов с экрана ТВ">
<button onclick=sendpin()>Отправить PIN</button></div>
<script>
async function refresh(){try{let r=await fetch('/api/pairing/status');let j=await r.json();
document.getElementById('st').textContent=
"идентичность: "+(j.identity_ready?"готова":"генерируется…")+"\nсопряжён: "+j.paired+
"\ncontrol готов: "+j.control_ready+"\nждём PIN: "+j.awaiting_pin+"\nтекущ. приложение: "+j.current_app;
} catch(e){document.getElementById('st').textContent='ошибка статуса';}}
async function pair(){await fetch('/api/pairing/start');refresh();}
async function sendpin(){let p=document.getElementById('pin').value.trim();await fetch('/api/pairing/pin?pin='+encodeURIComponent(p));refresh();}
refresh();setInterval(refresh,2000);
</script></body></html>)rawliteral";
    server.send(200, "text/html", page);
}

void setup() {
    Serial.begin(115200);
    bootAt = millis();

    Serial.println("\n===============================================");
    Serial.println("  Alice TV Bridge (ESP32)                     ");
    Serial.println("===============================================");

    config.load();
    Serial.printf("[CFG] wifi='%s' tv=%s:%u transport=%s\n",
                  config.wifiSsid.c_str(), config.tvIp.toString().c_str(), config.tvPort, config.transport.c_str());

    if (config.transport == "remote_v2") {
        tv = &remoteV2;
        Serial.println("[CFG] транспорт: Google TV Remote v2 (control 6466 / pairing 6467).");
        // Тяжёлый RSA-keygen уводим в фоновую задачу, чтобы loop() не блокировался.
        if (!deviceIdentity.isReady()) {
            xTaskCreate(identityTask, "idgen", 16384, NULL, 1, NULL);
        }
    } else {
        tv = &adb;
    }

    if (config.hasWifi()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(config.wifiSsid.c_str(), config.wifiPass.c_str());
        Serial.printf("[WiFi] Подключение к '%s'...", config.wifiSsid.c_str());
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
            delay(200);
            Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[WiFi] Не подключено; поднимаю точку доступа настройки.");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(AP_SSID);
            Serial.printf("[WiFi] AP '%s', страница настройки: http://%s/setup\n", AP_SSID, WiFi.softAPIP().toString().c_str());
        }
    } else {
        Serial.println("[WiFi] Нет сохранённых креденшелов; поднимаю точку доступа настройки.");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID);
        Serial.printf("[WiFi] AP '%s', страница настройки: http://%s/setup\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    }

    // Запускаем веб-сервер до сетевых запросов к ТВ, чтобы UI был доступен немедленно
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/cmd", HTTP_GET, handleCmd);
    server.on("/api/wake", HTTP_GET, handleWake);
    server.on("/api/search", HTTP_GET, handleSearch);
    server.on("/api/key", HTTP_GET, handleKey);
    server.on("/api/shell", HTTP_GET, handleShell);
    server.on("/api/apps", HTTP_GET, handleApps);
    server.on("/api/packages", HTTP_GET, handlePackages);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/debug/identity", HTTP_GET, handleDebugIdentity);
    server.on("/pair", HTTP_GET, handlePairingPage);
    server.on("/api/pairing/status", HTTP_GET, handlePairingStatus);
    server.on("/api/pairing/start", HTTP_ANY, handlePairingStart);
    server.on("/api/pairing/pin", HTTP_ANY, handlePairingPin);
    server.on("/setup", HTTP_GET, handleSetupForm);
    server.on("/setup", HTTP_POST, handleSetupSave);

    server.begin();
    Serial.println("[HTTP] Web-сервер запущен на порту 80.");

    // Инициализация соединения с ТВ (фоновое или отложенное)
    if (config.isTvConfigured()) {
        Serial.println("[TV] Подключение к телевизору...");
        if (tv->begin(config.tvIp, config.tvPort)) {
            Serial.println("[TV] Подключено.");
        } else {
            Serial.println("[TV] Предупреждение: подключение будет повторено при первой команде.");
        }
    }
}

void loop() {
    server.handleClient();
    tvTick(); // неблокирующее обслуживание транспорта (пока no-op для ADB)

    // Дополнительное управление через монитор порта
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '+' || c == '=') tv->key("vol_up");
        else if (c == '-' || c == '_') tv->key("vol_down");
        else if (c == 'p' || c == 'P') tv->key("power");
        else if (c == 'h' || c == 'H') tv->key("home");
        else if (c == 'b' || c == 'B') tv->key("back");
        else if (c == 'o' || c == 'O' || c == ' ') tv->key("ok");
        else if (c == 'w' || c == 'W') tv->key("up");
        else if (c == 's' || c == 'S') tv->key("down");
        else if (c == 'a' || c == 'A') tv->key("left");
        else if (c == 'd' || c == 'D') tv->key("right");
    }

    yield(); // освобождаем планировщик без блокирующей задержки (AGENTS 6.1)
}
#endif
