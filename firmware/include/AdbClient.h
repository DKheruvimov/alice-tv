#ifndef ADB_CLIENT_H
#define ADB_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>

class AdbShellResult {
public:
    bool ok = false;
    String output;
};

class AdbClient {
public:
    AdbClient();
    bool begin(IPAddress tvIp, uint16_t port = 5555);
    bool sendKey(int keycode);
    bool sendShell(const String &cmd);
    AdbShellResult execShell(const String &cmd, uint32_t timeoutMs = 8000);
    bool isConnected();

private:
    IPAddress _tvIp;
    uint16_t _port;
    bool _connected;
    uint32_t _localId;

    bool connectAdb();
    AdbShellResult execShellOnce(const String &cmd, uint32_t timeoutMs);
    bool signToken(const uint8_t *token, size_t tokenLen, uint8_t *signature, size_t &sigLen);
    bool sendPacket(uint32_t cmd, uint32_t arg0, uint32_t arg1, const uint8_t *data, size_t len);
    bool readPacket(uint32_t &cmd, uint32_t &arg0, uint32_t &arg1, uint8_t *data, size_t &len, size_t maxLen, uint32_t timeoutMs = 3000);
};

#endif
