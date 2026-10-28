#include "config/Config.h"
#include <Preferences.h>

// Опциональные дев-дефолты для отладочного стенда. Файл gitignore-ён и НЕ попадает
// в репозиторий; продакшн получает эти значения из NVS через портал /setup.
#if defined(__has_include)
#if __has_include("config/credentials.h")
#include "config/credentials.h"
#define ALIVETV_HAVE_DEV_CREDS 1
#endif
#endif

static const char* NVS_NS = "alivetv";

Config config;

String Config::nvStr(const char* key, const String& def) {
    Preferences p;
    if (!p.begin(NVS_NS, true)) return def;
    String v = p.getString(key, def.c_str());
    p.end();
    return v;
}

void Config::putStr(const char* key, const String& val) {
    Preferences p;
    if (!p.begin(NVS_NS, false)) return;
    p.putString(key, val.c_str());
    p.end();
}

void Config::load() {
    Preferences p;
    bool rw = p.begin(NVS_NS, true);

    auto getStr = [&](const char* k, const char* d) -> String {
        return rw ? String(p.getString(k, d)) : String(d);
    };
    auto getU16 = [&](const char* k, uint16_t d) -> uint16_t {
        return rw ? (uint16_t)p.getUShort(k, d) : d;
    };

    wifiSsid = getStr("wifi_ssid", "");
    wifiPass = getStr("wifi_pass", "");
    String ipStr = getStr("tv_ip", "");
    tvPort = getU16("tv_port", 5555);
    transport = getStr("transport", "adb");

    mqttServer = getStr("mqtt_server", "");
    mqttPort = getU16("mqtt_port", 1883);
    mqttUser = getStr("mqtt_user", "");
    mqttPass = getStr("mqtt_pass", "");
    mqttTopicRoot = getStr("mqtt_root", "devices");

    if (rw) p.end();

    // Дев-фолбэк: если в NVS пусто и есть gitignore-ёный credentials.h — подставить,
    // чтобы отладочный стенд поднимался без ручного провижининга.
#ifdef ALIVETV_HAVE_DEV_CREDS
    if (wifiSsid.length() == 0 && strlen(DEV_WIFI_SSID) > 0) {
        wifiSsid = DEV_WIFI_SSID;
        wifiPass = DEV_WIFI_PASS;
    }
    if (!isTvConfigured()) {
        tvIp = IPAddress(DEV_TV_IP);
    }
#endif

    if (ipStr.length() > 0) {
        tvIp = IPAddress();
        tvIp.fromString(ipStr);
    }
    if (tvPort == 0) tvPort = 5555;
    if (transport.length() == 0) transport = "adb";
}

bool Config::save() {
    Preferences p;
    if (!p.begin(NVS_NS, false)) return false;
    p.putString("wifi_ssid", wifiSsid.c_str());
    p.putString("wifi_pass", wifiPass.c_str());
    p.putString("tv_ip", tvIp.toString().c_str());
    p.putUShort("tv_port", tvPort);
    p.putString("transport", transport.c_str());
    p.putString("mqtt_server", mqttServer.c_str());
    p.putUShort("mqtt_port", mqttPort);
    p.putString("mqtt_user", mqttUser.c_str());
    p.putString("mqtt_pass", mqttPass.c_str());
    p.putString("mqtt_root", mqttTopicRoot.c_str());
    p.end(); // в Arduino-ESP32 end() фиксирует изменения в NVS
    return true;
}

void Config::setWifi(const String& ssid, const String& pass) {
    wifiSsid = ssid;
    wifiPass = pass;
}

void Config::setTv(const IPAddress& ip, uint16_t port) {
    tvIp = ip;
    tvPort = port ? port : 5555;
}

void Config::setTransport(const String& t) {
    transport = t;
}
