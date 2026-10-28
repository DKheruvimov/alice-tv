#include "utils.h"
#include <ESPmDNS.h>

void printPacket(uint8_t *packet, size_t len) {
    Serial.print("unit8array: [");
    for (size_t i = 0; i < len; i++) {
        Serial.printf("0x%02X", packet[i]);
        if (i < len - 1) {
            Serial.print(", ");
        }
    }
    Serial.println("]");

    Serial.print("hex: ");
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X", packet[i]);
    }
    Serial.println();

}

#ifndef LED_BUILTIN
#define LED_BUILTIN 8
#endif

IPAddress findTVIp(String macAddress) {
    MDNS.begin("esp32");
    bool found = false;
    IPAddress ip;

    Serial.println("[mDNS] Searching for Android TV (service 'androidtvremote2')...");
    while (!found)
    {
        int len = MDNS.queryService("androidtvremote2", "tcp");
        for (int i = 0; i < len; i++) {
            Serial.print("host_name = ");
            Serial.print(MDNS.hostname(i));
            ip = MDNS.address(i);
            Serial.print(", IP = ");
            Serial.print(ip.toString());
            if (MDNS.hasTxt(i, "bt")) {
                String mc = MDNS.txt(i, "bt");
                Serial.print(", mac = ");
                Serial.print(mc);

                if (macAddress.length() == 0 || mc.equalsIgnoreCase(macAddress)) {
                    found = true;
                    Serial.println(" -> Selected!");
                    break;
                }
            } else if (macAddress.length() == 0) {
                found = true;
                Serial.println(" -> Selected!");
                break;
            }
            Serial.println();
        }
        delay(1000);
    }
    Serial.println();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    Serial.printf("Found TV IP: %s\n", ip.toString().c_str());
    MDNS.end();
    return ip;
}