#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

// Per-device identity для Google TV Remote Protocol v2 (P1b).
//
// Эталон (tronikos/androidtvremote2/src/.../certificate_generator.py) генерирует
// self-signed RSA-2048 сертификат (CN=hostname, BasicConstraints CA=true pathLen=0,
// serial=1000, ~10 лет, подпись SHA256) на хосте через python-cryptography и использует
// его как клиентский сертификат в mutual-TLS на 6466/6467. TV не проверяет цепочку
// (WOLFSSL_VERIFY_NONE), а pairing-secret хеширует только modulus/exponent публичного
// ключа — поэтомуExtensions (SAN) функционально не обязательны, и мы их опускаем.
//
// Здесь то же самое делается НА УСТРОЙСТВЕ через wolfSSL (wc_MakeRsaKey + wc_InitCert +
// wc_MakeCertReq + wc_SignCert), один раз при первой идентичности, и сохраняется в NVS.
// При последующих загрузках DER просто читается из NVS — ключ/сертификат стабильны,
// что важно: TV запоминает спаренный публичный ключ.
//
// ВНИМАНИЕ: генерация RSA-2048 на ESP32-C3 (без HW-ускорителя RSA) занимает секунды,
// поэтому generate() временно отвязывает текущую задачу от watchdog.

#include <Arduino.h>
#include <stdint.h>

class DeviceIdentity {
public:
    // Гарантирует готовность идентичности: сначала NVS, иначе — генерация + сохранение.
    // Возвращает true, если сертификат и ключ доступны в памяти после вызова.
    bool ensure(const String& commonName);

    bool isReady() const { return _cert != nullptr && _certLen > 0 && _key != nullptr && _keyLen > 0; }

    const uint8_t* certDer() const { return _cert; }
    uint32_t       certLen() const { return _certLen; }
    const uint8_t* keyDer()  const { return _key; }
    uint32_t       keyLen()  const { return _keyLen; }

    // Стирание сохранённой идентичности из NVS (для пере-спаривания / factory reset).
    void wipe();

private:
    bool loadFromNvs();
    bool generate(const String& commonName);
    bool saveToNvs();

    uint8_t* _cert = nullptr;
    uint32_t _certLen = 0;
    uint8_t* _key = nullptr;
    uint32_t _keyLen = 0;
};

extern DeviceIdentity deviceIdentity;

#endif // DEVICE_IDENTITY_H
