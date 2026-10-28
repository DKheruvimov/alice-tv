#ifndef WAKEUP_HELPER_H
#define WAKEUP_HELPER_H

#include <Arduino.h>
#include <WiFi.h>
#include "transport/ITvTransport.h"

// Каскадный Wakeup Pipeline (AGENTS.md 2.2):
// Позволяет разбудить телевизор Android TV / Google TV из глубокого сна
// независимо от активного транспорта (ADB или Google TV Remote v2 / TLS).
class WakeupHelper {
public:
    // Парсинг MAC-адреса из строки вида "18:ef:3a:89:2b:c9" или "18-ef-3a-89-2b-c9"
    static bool parseMac(const String& macStr, uint8_t mac[6]);

    // Отправка Magic Packet (WoWLAN / Wake-on-Wireless-LAN)
    static bool sendMagicPacket(const uint8_t mac[6], IPAddress broadcastIp = IPAddress(255, 255, 255, 255), uint16_t port = 9);

    // Отправка HTTP-триггера Google Cast (порт 8008)
    static bool sendCastPing(IPAddress tvIp, uint16_t port = 8008);

    // Каскадный пайплайн пробуждения:
    // 1. Если сокет транспорта активен -> отправка key("power")
    // 2. Если ТВ спит -> отправка WoWLAN Magic Packet + Cast Ping -> попытка переподключения
    static TvResult wake(ITvTransport* tv, IPAddress tvIp, uint16_t tvPort, const String& macStr);
};

#endif
