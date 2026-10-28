#include "AdbClient.h"
#include <WiFiClient.h>
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/md.h"
#include "esp_random.h"

#define A_CNXN 0x4e584e43
#define A_AUTH 0x48545541
#define A_OPEN 0x4e45504f
#define A_OKAY 0x59414b4f
#define A_CLSE 0x45534c43
#define A_WRTE 0x45545257

#define ADB_AUTH_TOKEN        1
#define ADB_AUTH_SIGNATURE    2
#define ADB_AUTH_RSAPUBLICKEY 3

static const char ADB_PRIVATE_KEY_PEM[] = 
"-----BEGIN PRIVATE KEY-----\n"
"MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCys2aDupJulHA/\n"
"EViUMm2GKKu89LYflYe/SfAX2ZHg3+0n7YNt9dTZ2OFSUO8Mg48rKlB5f9zEsmoE\n"
"zVFCvU5DligjNmR+0OdJOLdCgTbz3MAH7QniOkCHcX/1VFYPaZLPyOhupypUC1RF\n"
"/pjXyZItfcPciCWunHgTsLSIGF7tQsAGU75qJEr2cCIqeUE4uDa2Xy0SwSUlWBmM\n"
"utfjksOqADgjXOJYyDJ5P4tSmkbLo+YZEuBL4c6omPOVdfxf9OfdgLUn7/XqEOxs\n"
"KC+GUnrJ2ahcFXD1q+HneTWO+5ERmEWfAH1gPpjcBv3Ll7ns3/ErcSxEjtYpwlTX\n"
"MTo2lEV7AgMBAAECggEAPi8ihcVnTlaWJsQs/Ej+BtjhRjwrohsBJWYfx9TcqBGU\n"
"L7g9a4bHnlUTt3bVcJfZOq6bWWROmR3yuaAO3ngMo4U9i19SojSkvzyevORFEKAp\n"
"LLjmHxShPuCWo/LIjc1xmp+xjkvadWuJ1F+PpUuN9AmXHgc8gaIOQN/VOqSsZ8gl\n"
"AhMsZYMroQzj8caDRp3A30iCSxsJOrrGNmDQC52hIhbSTAMwBHGeFK9d3C9a51IF\n"
"Rc8/EC5QxAPY9kdlN8fMbwE5oaE1mgHN6oFcDC9bB76kw3SNuuTRmVdcEGQm9L2V\n"
"Uzk/KwluhNs6h50n7Xsu54WSnwC/EEKmuiv3PJTEeQKBgQDhvAeV3KBejCig7aQn\n"
"I9sumtY6zXwqiCzc4WIm3ZITH2iZdholkohjGYaGVjVouiEeLq2k4vVwkkjQgyGI\n"
"lYO7mvbefd2KK0Y/sKy0+iDqUgC6v8SReRgiSrY0HLBvKgHru5dFe9Wk93hDT+6R\n"
"9m6MM15SL10RRdCElTX4PxUuQwKBgQDKqQPbLg3ZEkWylovG+Ru+Bn6NAst17Vtz\n"
"WKpveUeTMiGdTyW98LxZfMTZunh9B16eDAJzR4BLw38kudYa84hHJCkrd80LqMvx\n"
"rkAKMUZePTiRcwOlYn6VSQsfwfrbIUNFf+cpmWJXIJxF67ATeY8+cmhr1QDwItOf\n"
"fyszxZPEaQKBgFvtV+V2yLRYPb9zp5Ls7jing7DEt9kWRbbZ7j/HodI5yz4bs4Az\n"
"kWlSHs+cUfnJOu1wsvRgHPXwDMklbcWSpegDrp9jAJJhFyWezGaTKk5igLuMlv9r\n"
"IiHs7RCB0nQi1k9qwKriSFn7EbXP2mcExXZBARtVAIClPsqPfUAQaI7DAoGAGJie\n"
"wTE7uQbAssYWvNVrCosbPN4D95IRMVSnitP7XeANuFvsxrq7AVllQs+g4/vK/f+T\n"
"JI6fAZL+kWSEmczI02mOHZpypmGcLv7pAtOsKK/hd7Wv0Zi8qD2vPfF7ErFcysiV\n"
"M7VL/CsXDF1oh1SPkBKunbfCX2K6sHbstWHbZ1ECgYBnk5Dxu1No+JNqCTQjG7JD\n"
"uFP/By3bTffCmAsjyaX/jmdhjTZg36uJkaF7OpeX7c4Nym5KwZhWPLUoqeIc5OZH\n"
"q32wj+oEM48kIfRsEdtnofhaKokUQyNeZxF9NfUxntP7eHoewg27N+cTK5g5rc7O\n"
"5vf42sENzyb0IWDVqFoFJQ==\n"
"-----END PRIVATE KEY-----\n";

static const char ADB_PUBLIC_KEY[] = 
"QAAAAE0uUU97RZQ2OjHXVMIp1o5ELHEr8d/suZfL/QbcmD5gfQCfRZgRkfuONXnn4av1cBVcqNnJelKGLyhs7BDq9e8ntYDd5/Rf/HWV85iozuFL4BIZ5qPLRppSiz95MshY4lwjOACqw5Lj17qMGVglJcESLV+2Nrg4QXkqInD2SiRqvlMGwELtXhiItLATeJyuJYjcw30tksnXmP5FVAtUKqdu6MjPkmkPVlT1f3GHQDriCe0HwNzzNoFCtzhJ59B+ZDYjKJZDTr1CUc0EarLE3H95UCorj4MM71BS4djZ1PVtg+0n7d/gkdkX8Em/h5UftvS8qyiGbTKUWBE/cJRukrqDZrOybVxmmpaJuAXFGvPWZIC8HubpFoM9TAl7jVpNLWgMxMmoGZNfH6TX7XW/pLbqh0rRIC6AzoLfzLnI+X2HJ3UyJmdZwEAVxtmbgoI8Te+jU2my6z4Hg9mAuF1sWrTqFtgNVMU3Ig2U0r24DB6UHwQ7QxSSsjoyk0Jahef39Zf0VXzdh9DdVbzhPLObeOp6tYo2nQhokbGQIJxbFTqkGlijQv0Q3R/Pvk8T8mYJecyuZgDc6+jLU4LksDDDC8gglwOiOYgqlwSyOWt6JX0OWc1z7yv79AbJ5L7dx0RjhwYgLINXK2lTRjL9Flpi3C9TUyJyQOI9fid8/LpeVmCh8kjaVgEAAQA= rusil@illusion";

static int my_random(void *p_rng, unsigned char *output, size_t output_len) {
    (void)p_rng;
    esp_fill_random(output, output_len);
    return 0;
}

static WiFiClient adbTcpClient;

AdbClient::AdbClient() : _port(5555), _connected(false), _localId(10) {}

bool AdbClient::begin(IPAddress tvIp, uint16_t port) {
    _tvIp = tvIp;
    _port = port;
    return connectAdb();
}

bool AdbClient::isConnected() {
    return _connected && adbTcpClient.connected();
}

bool AdbClient::signToken(const uint8_t *token, size_t tokenLen, uint8_t *signature, size_t &sigLen) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_parse_key(&pk, (const unsigned char*)ADB_PRIVATE_KEY_PEM, strlen(ADB_PRIVATE_KEY_PEM) + 1, NULL, 0, my_random, NULL);
    if (ret != 0) {
        Serial.printf("[ADB] mbedtls_pk_parse_key error: -0x%04x\n", -ret);
        mbedtls_pk_free(&pk);
        return false;
    }

    ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA1, token, tokenLen, signature, 256, &sigLen, my_random, NULL);
    mbedtls_pk_free(&pk);
    if (ret != 0) {
        Serial.printf("[ADB] mbedtls_pk_sign error: -0x%04x\n", -ret);
        return false;
    }
    return true;
}

bool AdbClient::sendPacket(uint32_t cmd, uint32_t arg0, uint32_t arg1, const uint8_t *data, size_t len) {
    uint32_t checksum = 0;
    if (data && len > 0) {
        for (size_t i = 0; i < len; i++) {
            checksum += data[i];
        }
    }
    uint32_t magic = cmd ^ 0xFFFFFFFF;

    uint32_t header[6] = { cmd, arg0, arg1, (uint32_t)len, checksum, magic };
    if (adbTcpClient.write((const uint8_t*)header, 24) != 24) {
        return false;
    }
    if (data && len > 0) {
        if (adbTcpClient.write(data, len) != len) {
            return false;
        }
    }
    adbTcpClient.flush();
    return true;
}

bool AdbClient::readPacket(uint32_t &cmd, uint32_t &arg0, uint32_t &arg1, uint8_t *data, size_t &len, size_t maxLen, uint32_t timeoutMs) {
    unsigned long start = millis();
    while (adbTcpClient.available() < 24) {
        if (!adbTcpClient.connected()) return false;
        if (millis() - start > timeoutMs) return false;
        delay(5);
    }

    uint32_t header[6];
    if (adbTcpClient.readBytes((char*)header, 24) != 24) return false;

    cmd = header[0];
    arg0 = header[1];
    arg1 = header[2];
    uint32_t dataLen = header[3];

    len = 0;
    if (dataLen > 0) {
        start = millis();
        while (adbTcpClient.available() < (int)dataLen) {
            if (!adbTcpClient.connected()) return false;
            if (millis() - start > timeoutMs) return false;
            delay(5);
        }
        size_t toRead = (dataLen < maxLen) ? dataLen : maxLen;
        len = adbTcpClient.readBytes((char*)data, toRead);
        // пропуск лишних байт если maxLen меньше
        if (dataLen > toRead) {
            size_t remaining = dataLen - toRead;
            while (remaining > 0) {
                adbTcpClient.read();
                remaining--;
            }
        }
    }
    return true;
}

bool AdbClient::connectAdb() {
    _connected = false;
    if (adbTcpClient.connected()) {
        adbTcpClient.stop();
    }

    Serial.printf("[ADB] Подключение к ТВ %s:%d...\n", _tvIp.toString().c_str(), _port);
    if (!adbTcpClient.connect(_tvIp, _port, 3000)) {
        Serial.println("[ADB] Ошибка подключения к порту 5555");
        return false;
    }

    // 1. Отправляем CNXN (CONNECT)
    const char banner[] = "host::AliceESP32";
    if (!sendPacket(A_CNXN, 0x01000000, 4096, (const uint8_t*)banner, strlen(banner) + 1)) {
        adbTcpClient.stop();
        return false;
    }

    // 2. Читаем ответ (обычно AUTH с токеном)
    uint32_t cmd, arg0, arg1;
    uint8_t buffer[512];
    size_t len = 0;

    if (!readPacket(cmd, arg0, arg1, buffer, len, sizeof(buffer), 8000)) {
        Serial.println("[ADB] Таймаут ожидания ответа на CNXN");
        adbTcpClient.stop();
        return false;
    }

    if (cmd == A_CNXN) {
        Serial.println("[ADB] Успешное прямое подключение (авторизация не требовалась)!");
        _connected = true;
        return true;
    }

    if (cmd == A_AUTH && arg0 == ADB_AUTH_TOKEN) {
        Serial.println("[ADB] Получен токен авторизации. Подпись ключом...");
        uint8_t signature[256];
        size_t sigLen = 0;
        if (!signToken(buffer, len, signature, sigLen)) {
            Serial.println("[ADB] Ошибка создания подписи RSA");
            adbTcpClient.stop();
            return false;
        }

        // Отправляем AUTH Signature
        if (!sendPacket(A_AUTH, ADB_AUTH_SIGNATURE, 0, signature, sigLen)) {
            adbTcpClient.stop();
            return false;
        }

        // Читаем подтверждение
        if (!readPacket(cmd, arg0, arg1, buffer, len, sizeof(buffer), 4000)) {
            Serial.println("[ADB] Таймаут после отправки подписи");
            adbTcpClient.stop();
            return false;
        }

        if (cmd == A_CNXN) {
            Serial.println("[ADB] Авторизация успешна! Соединение установлено.");
            _connected = true;
            return true;
        } else if (cmd == A_AUTH) {
            // Если подпись не подошла, посылаем публичный ключ
            Serial.println("[ADB] Отправка открытого ключа на ТВ...");
            size_t pubLen = strlen(ADB_PUBLIC_KEY) + 1;
            sendPacket(A_AUTH, ADB_AUTH_RSAPUBLICKEY, 0, (const uint8_t*)ADB_PUBLIC_KEY, pubLen);

            if (readPacket(cmd, arg0, arg1, buffer, len, sizeof(buffer), 6000)) {
                if (cmd == A_CNXN) {
                    Serial.println("[ADB] Ключ подтвержден! Подключено.");
                    _connected = true;
                    return true;
                }
            }
        }
    }

    Serial.printf("[ADB] Не удалось завершить авторизацию (cmd=0x%08x)\n", cmd);
    adbTcpClient.stop();
    return false;
}

AdbShellResult AdbClient::execShellOnce(const String &cmd, uint32_t timeoutMs) {
    AdbShellResult result;

    uint32_t myId = ++_localId;
    String openPayload = "shell:" + cmd;
    size_t payloadLen = openPayload.length() + 1; // с завершающим \0
    if (!sendPacket(A_OPEN, myId, 0, (const uint8_t*)openPayload.c_str(), payloadLen)) {
        _connected = false;
        adbTcpClient.stop();
        return result;
    }

    uint32_t rCmd, rArg0, rArg1;
    uint8_t buffer[4096]; // штатный максимум payload одного ADB WRTE — иначе хвост вывода теряется
    size_t len = 0;
    bool streamOpen = false;
    unsigned long start = millis();

    // Читаем до CLSE от нашего стрима: OKAY(открытие) -> WRTE(вывод)... -> CLSE
    while (millis() - start < timeoutMs) {
        if (!readPacket(rCmd, rArg0, rArg1, buffer, len, sizeof(buffer), 1000)) {
            break;
        }

        if (rCmd == A_OKAY && rArg1 == myId) {
            streamOpen = true;
        } else if (rCmd == A_WRTE && rArg1 == myId) {
            // подтверждаем получение вывода, иначе ТВ заблокируется на следующем WRTE
            if (!sendPacket(A_OKAY, myId, rArg0, NULL, 0)) {
                break;
            }
            if (len > 0) {
                if (result.output.length() < 8192) {
                    result.output.concat((const char*)buffer, len);
                }
            }
        } else if (rCmd == A_CLSE && (rArg1 == myId || rArg0 == myId)) {
            result.ok = streamOpen;
            return result;
        }
    }

    // Успех определяется ОТКРЫТИЕМ стрима (A_OKAY), а не наличием вывода:
    // команды вида `input keyevent` штатно возвращают 0 байт, и трактовать пустой
    // вывод как сбой — ложная ошибка, рвущая живое соединение при каждом нажатии.
    result.ok = streamOpen;
    return result;
}

AdbShellResult AdbClient::execShell(const String &cmd, uint32_t timeoutMs) {
    if (!isConnected()) {
        if (!connectAdb()) {
            return AdbShellResult();
        }
    }

    AdbShellResult r = execShellOnce(cmd, timeoutMs);
    if (r.ok) return r;

    // Стрим не открылся: вероятнее всего гонка после простоя (ТВ успел закрыть
    // TCP). Переподключаемся и повторяем ОДИН раз. Если же стрим открылся, но
    // команда вернула текст ошибки — это легитимный ответ, повтор не нужен.
    Serial.println("[ADB] Стрим не открылся, переподключение + повтор");
    _connected = false;
    adbTcpClient.stop();
    if (!connectAdb()) return r;
    return execShellOnce(cmd, timeoutMs);
}

bool AdbClient::sendShell(const String &cmd) {
    return execShell(cmd).ok;
}

bool AdbClient::sendKey(int keycode) {
    String cmd = "input keyevent " + String(keycode);
    return sendShell(cmd);
}
