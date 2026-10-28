#include "transport/WakeupHelper.h"
#include <WiFiUdp.h>
#include <WiFiClient.h>

bool WakeupHelper::parseMac(const String& macStr, uint8_t mac[6]) {
    int values[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6 ||
        sscanf(macStr.c_str(), "%x-%x-%x-%x-%x-%x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; ++i) mac[i] = (uint8_t)values[i];
        return true;
    }
    return false;
}

bool WakeupHelper::sendMagicPacket(const uint8_t mac[6], IPAddress broadcastIp, uint16_t port) {
    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 1; i <= 16; i++) {
        memcpy(&packet[i * 6], mac, 6);
    }

    WiFiUDP udp;
    if (!udp.begin(0)) return false;

    // Отправляем в общий broadcast и локальную подсеть
    bool ok = udp.beginPacket(broadcastIp, port);
    if (ok) {
        udp.write(packet, sizeof(packet));
        udp.endPacket();
    }

    IPAddress subnetBroadcast = WiFi.broadcastIP();
    if (subnetBroadcast[0] != 0 && subnetBroadcast != broadcastIp) {
        if (udp.beginPacket(subnetBroadcast, port)) {
            udp.write(packet, sizeof(packet));
            udp.endPacket();
        }
    }
    udp.stop();

    Serial.printf("[WAKE] WoWLAN Magic Packet отправлен на %02x:%02x:%02x:%02x:%02x:%02x (порт %u)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], port);
    return true;
}

bool WakeupHelper::sendCastPing(IPAddress tvIp, uint16_t port) {
    WiFiClient client;
    if (!client.connect(tvIp, port, 400)) {
        return false;
    }
    client.print("GET /setup/eureka_info HTTP/1.1\r\nHost: tv\r\nConnection: close\r\n\r\n");
    client.stop();
    Serial.printf("[WAKE] Cast Ping успешно отправлен на %s:%u\n", tvIp.toString().c_str(), port);
    return true;
}

TvResult WakeupHelper::wake(ITvTransport* tv, IPAddress tvIp, uint16_t tvPort, const String& macStr) {
    TvResult res;

    // 1. Если транспорт уже на связи — отправляем команду power
    if (tv && tv->isConnected()) {
        Serial.println("[WAKE] ТВ уже подключен, отправка key(power)...");
        res = tv->key("power");
        return res;
    }

    // 2. ТВ спит или сокет не активен: запускаем каскадный Wakeup Pipeline
    Serial.println("[WAKE] ТВ спит или не отвечает. Запуск каскадного Wakeup Pipeline...");

    uint8_t mac[6];
    bool haveMac = parseMac(macStr, mac);
    if (haveMac) {
        sendMagicPacket(mac, IPAddress(255, 255, 255, 255), 9);
        sendMagicPacket(mac, IPAddress(255, 255, 255, 255), 7);
    } else {
        Serial.println("[WAKE] Предупреждение: MAC-адрес ТВ не задан или не распознан");
    }

    // Пробуем запустить Google Cast для включения экрана
    sendCastPing(tvIp, 8008);

    // Инициируем подключение транспорта
    if (tv) {
        tv->begin(tvIp, tvPort);
    }

    res.ok = true;
    res.detail = "wake packets sent (WoWLAN + Cast)";
    return res;
}
