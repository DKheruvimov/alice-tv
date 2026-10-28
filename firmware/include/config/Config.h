#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <WiFiType.h>

// Конфигурация устройства на NVS/Preferences (AGENTS.md 6.3: секреты не в коде).
// Источник истины рантайма — NVS. Значения по умолчанию для отладочного стенда
// подхватываются из gitignore-ёного credentials.h и только для первичного сина;
// в репозиторий секреты не попадают.
class Config {
public:
    String wifiSsid;
    String wifiPass;
    IPAddress tvIp;
    uint16_t tvPort = 5555;
    String transport = "adb"; // "adb" | "remote_v2"

    // MQTT (контракт появится на этапе P4; поля закладываются сейчас).
    String mqttServer;
    uint16_t mqttPort = 1883;
    String mqttUser;
    String mqttPass;
    String mqttTopicRoot = "devices";

    // Загрузить значения из NVS (с дев-фолбэком из credentials.h, если задан).
    void load();
    // Сохранить текущие поля в NVS.
    bool save();

    bool hasWifi() const { return wifiSsid.length() > 0; }
    bool isTvConfigured() const { return tvIp[0] != 0 || tvIp[1] != 0 || tvIp[2] != 0 || tvIp[3] != 0; }

    void setWifi(const String& ssid, const String& pass);
    void setTv(const IPAddress& ip, uint16_t port);
    void setTransport(const String& t);

private:
    static String nvStr(const char* key, const String& def);
    static void putStr(const char* key, const String& val);
};

extern Config config;

#endif
