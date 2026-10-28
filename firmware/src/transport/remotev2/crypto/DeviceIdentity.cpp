#include "transport/remotev2/crypto/DeviceIdentity.h"

#include <Preferences.h>
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include "esp_task_wdt.h"
#include "esp_system.h"

// Источник энтропии для wolfSSL (hook CUSTOM_RAND_GENERATE_BLOCK, см. user_settings.h).
// Аппаратный TRNG ESP32-C3: esp_random() возвращает 32-битное значение, заполняем побайтно.
// Вызывается из wc_InitRng/wc_GenerateSeed и напрямую из генератора ключей.
extern "C" int wc_esp_rand_block(unsigned char* output, unsigned int sz) {
    for (unsigned int i = 0; i < sz; i++) {
        output[i] = (unsigned char)(esp_random() & 0xFF);
    }
    return 0;
}

// Отдельный NVS-неймспейс для идентичности, чтобы не смешивать с пользовательским
// конфигом и иметь возможность точечно стирать при factory reset.
static const char* ID_NVS_NS = "rtv2id";
static const char* ID_NVS_CERT = "cert"; // blob: X.509 DER
static const char* ID_NVS_KEY  = "key";  // blob: PKCS#1 RSAPrivateKey DER

DeviceIdentity deviceIdentity;

// Верхняя граница буфера чтения blob из NVS. RSA-2048 PKCS#1 DER ~1218 байт,
// self-signed cert ~760 байт; 4096 — с запасом.
static const size_t kNvsBlobCap = 4096;

// Верхняя граница DER-буфера сборки сертификата. Self-signed RSA-2048 сертификат
// с CN и basicConstraints весит ~760 байт; 2048 — с большим запасом.
static const size_t kCertDerCap = 2048;

bool DeviceIdentity::ensure(const String& commonName) {
    if (isReady()) return true;
    if (loadFromNvs()) {
        Serial.printf("[ID]: Идентичность загружена из NVS (cert=%u, key=%u)\n", _certLen, _keyLen);
        return true;
    }
    Serial.println("[ID]: Идентичности нет — генерирую RSA-2048 + self-signed cert (несколько секунд)...");
    unsigned long t0 = millis();
    if (!generate(commonName)) {
        Serial.println("[ID]: Ошибка генерации идентичности");
        return false;
    }
    if (!saveToNvs()) {
        Serial.println("[ID]: ВНИМАНИЕ: не удалось сохранить идентичность в NVS (будет перегенерирована после ребута)");
    }
    Serial.printf("[ID]: Сгенерировано за %lu мс (cert=%u, key=%u)\n", millis() - t0, _certLen, _keyLen);
    return isReady();
}

bool DeviceIdentity::loadFromNvs() {
    Preferences p;
    if (!p.begin(ID_NVS_NS, true)) return false;
    uint8_t* certScr = (uint8_t*)malloc(kNvsBlobCap);
    uint8_t* keyScr  = (uint8_t*)malloc(kNvsBlobCap);
    bool ok = false;
    if (certScr && keyScr) {
        // getBytes возвращает число реально прочитанных байт (0, если ключа нет).
        size_t certSz = p.getBytes(ID_NVS_CERT, certScr, kNvsBlobCap);
        size_t keySz  = p.getBytes(ID_NVS_KEY, keyScr, kNvsBlobCap);
        if (certSz > 0 && keySz > 0) {
            free(_cert); free(_key);
            _cert = certScr; _certLen = (uint32_t)certSz;
            _key = keyScr;   _keyLen = (uint32_t)keySz;
            certScr = nullptr; keyScr = nullptr; // владение передано объекту
            ok = true;
        }
    }
    free(certScr);
    free(keyScr);
    p.end();
    return ok;
}

bool DeviceIdentity::saveToNvs() {
    if (!isReady()) return false;
    Preferences p;
    if (!p.begin(ID_NVS_NS, false)) return false;
    size_t wc = p.putBytes(ID_NVS_CERT, _cert, _certLen);
    size_t wk = p.putBytes(ID_NVS_KEY, _key, _keyLen);
    p.end(); // фиксирует запись в NVS
    return (wc == _certLen) && (wk == _keyLen);
}

bool DeviceIdentity::generate(const String& commonName) {
    // Все локалы объявлены и проинициализированы ДО первого goto, чтобы не
    // пересекать инициализацию (запрещено в C++).
    bool ok = false;
    int ret = 0;
    bool wdtRemoved = false;
    WC_RNG rng;
    RsaKey key;
    Cert* cert = nullptr;
    byte* derBuf = nullptr;
    byte* keyDer = nullptr;
    int certDerSz = 0;
    int keyDerSz = 0;
    int writtenKeySz = 0;
    unsigned long gT0 = millis();

    memset(&rng, 0, sizeof(rng));
    memset(&key, 0, sizeof(key));

    // Генерация RSA-2048 на ESP32-C3 выполняется в софте и занимает секунды —
    // временно отвязываем текущую задачу от watchdog, чтобы не словить сброс.
    if (esp_task_wdt_delete(NULL) == ESP_OK) wdtRemoved = true;

    if (wc_InitRng(&rng) != 0) { Serial.println("[ID]: wc_InitRng failed"); goto done; }
    if (wc_InitRsaKey(&key, NULL) != 0) { Serial.println("[ID]: wc_InitRsaKey failed"); goto done; }

    // 65537 = WC_RSA_EXPONENT — совпадает с эталоном (public_exponent=65537).
    Serial.println("[ID]: ... Generating RSA-2048 keypair (wc_MakeRsaKey)...");
    ret = wc_MakeRsaKey(&key, 2048, WC_RSA_EXPONENT, &rng);
    if (ret != 0) { Serial.printf("[ID]: wc_MakeRsaKey failed (%d)\n", ret); goto done; }
    Serial.printf("[ID]: RSA keypair готов за %lu мс — собираю и подписываю cert...\n", millis() - gT0);

    cert = (Cert*)malloc(sizeof(Cert));
    derBuf = (byte*)malloc(kCertDerCap);
    if (!cert || !derBuf) { Serial.println("[ID]: no mem for cert"); goto done; }

    if (wc_InitCert(cert) != 0) { Serial.println("[ID]: wc_InitCert failed"); goto done; }
    strncpy((char*)cert->subject.commonName, commonName.c_str(), CTC_NAME_SIZE - 1);
    cert->subject.commonName[CTC_NAME_SIZE - 1] = '\0';
    cert->isCA = 1;              // BasicConstraints CA=true (как в эталоне)
    cert->pathLen = 0;           // path_length=0
    cert->sigType = CTC_SHA256wRSA;
    cert->daysValid = 3650;      // ~10 лет
    // serial_number = 1000 = 0x03E8 (старший байт со знакомым битом чистым — padding не нужен).
    cert->serial[0] = 0x03;
    cert->serial[1] = 0xE8;
    cert->serialSz = 2;

    // Даты действия задаём ЯВНО широким окном, потому что у устройства нет RTC/NTP
    // и wolfSSL иначе вычисляет notBefore от time()≈1970 → сертификат сразу «просрочен»
    // и ТВ отклонит его при mutual-TLS. RFC 5280: годы до 2050 кодируются как UTCTime
    // (tag 0x17) в формате YYMMDDHHMMSSZ. beforeDate/afterDate хранят готовый DER-TLV,
    // который EncodeCert копирует как есть (см. asn.c CopyValidity/beforeDateSz).
    // notBefore = 2024-01-01 00:00:00Z, notAfter = 2044-01-01 00:00:00Z.
    {
        static const byte kNotBefore[] = {0x17, 0x0D,
            '2','4','0','1','0','1','0','0','0','0','0','0','Z'};
        static const byte kNotAfter[] = {0x17, 0x0D,
            '4','4','0','1','0','1','0','0','0','0','0','0','Z'};
        memcpy(cert->beforeDate, kNotBefore, sizeof(kNotBefore));
        cert->beforeDateSz = (int)sizeof(kNotBefore);
        memcpy(cert->afterDate, kNotAfter, sizeof(kNotAfter));
        cert->afterDateSz = (int)sizeof(kNotAfter);
    }

    // Собираем И самозаверяем сертификат одной операцией. wc_MakeCert writes только
    // TBSCertificate (без signatureValue); wc_MakeSelfCert = wc_MakeCert + wc_SignCert,
    // который дописывает AlgorithmIdentifier + BIT STRING подписи (256 байт для RSA-2048).
    ret = wc_MakeSelfCert(cert, derBuf, (word32)kCertDerCap, &key, &rng);
    if (ret < 0) { Serial.printf("[ID]: wc_MakeSelfCert failed (%d)\n", ret); goto done; }
    certDerSz = ret;
    Serial.printf("[ID]: cert собран+подписан за %lu мс (size=%d)\n", millis() - gT0, certDerSz);

    // Приватный ключ в PKCS#1 DER (wolfSSL_CTX_use_PrivateKey_buffer ждёт ASN.1).
    keyDerSz = wc_RsaKeyToDer(&key, NULL, 0); // size-query
    if (keyDerSz <= 0) { Serial.printf("[ID]: wc_RsaKeyToDer size failed (%d)\n", keyDerSz); goto done; }
    keyDer = (byte*)malloc((size_t)keyDerSz);
    if (!keyDer) { Serial.println("[ID]: no mem for key der"); goto done; }
    ret = wc_RsaKeyToDer(&key, keyDer, (word32)keyDerSz);
    if (ret < 0) { Serial.printf("[ID]: wc_RsaKeyToDer failed (%d)\n", ret); goto done; }
    writtenKeySz = ret;

    // Публикуем в объект только после полного успеха.
    {
        uint8_t* ncert = (uint8_t*)malloc((size_t)certDerSz);
        uint8_t* nkey  = (uint8_t*)malloc((size_t)writtenKeySz);
        if (ncert && nkey) {
            memcpy(ncert, derBuf, (size_t)certDerSz);
            memcpy(nkey, keyDer, (size_t)writtenKeySz);
            free(_cert); free(_key);
            _cert = ncert; _certLen = (uint32_t)certDerSz;
            _key = nkey;   _keyLen = (uint32_t)writtenKeySz;
            ok = true;
        } else {
            free(ncert); free(nkey);
            Serial.println("[ID]: no mem to hold identity");
        }
    }

done:
    free(cert);        // Cert — Plain-структура без внутренних heap-указателей.
    wc_FreeRsaKey(&key);
    wc_FreeRng(&rng);
    free(derBuf);
    free(keyDer);
    if (wdtRemoved) esp_task_wdt_add(NULL);
    return ok;
}

void DeviceIdentity::wipe() {
    Preferences p;
    if (p.begin(ID_NVS_NS, false)) {
        p.remove(ID_NVS_CERT);
        p.remove(ID_NVS_KEY);
        p.end();
    }
    free(_cert); _cert = nullptr; _certLen = 0;
    free(_key);  _key = nullptr;  _keyLen = 0;
    Serial.println("[ID]: Идентичность стёрта");
}
