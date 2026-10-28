#include "RemoteClient.h"
#include "certificate/CertificateGenerator.h"
#include "transport/remotev2/crypto/DeviceIdentity.h"

WiFiClient client;

static WOLFSSL_CTX* ctx = NULL;
static WOLFSSL* ssl = NULL;
static char wc_error_message[80 + 1];

static int EthernetSend(WOLFSSL* ssl, char* msg, int sz, void* ctx) {
    (void)ssl;
    (void)ctx;
    int sent = client.write((const uint8_t*)msg, sz);
    return (sent > 0) ? sent : WOLFSSL_CBIO_ERR_CONN_CLOSE;
}

static int EthernetReceive(WOLFSSL* ssl, char* reply, int sz, void* ctx) {
    (void)ssl;
    (void)ctx;

    if (!client.connected() && client.available() == 0) {
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    }

    // Ожидаем поступления байтов до 3000 мс
    unsigned long start = millis();
    while (client.available() == 0 && (millis() - start < 3000)) {
        if (!client.connected()) {
            return WOLFSSL_CBIO_ERR_CONN_CLOSE;
        }
        delay(5);
    }

    if (client.available() == 0) {
        return WOLFSSL_CBIO_ERR_WANT_READ;
    }

    int ret = 0;
    while (client.available() > 0 && ret < sz) {
        reply[ret++] = client.read();
    }
    return ret;
}

int setup_wolfssl(void) {
    int ret = 0;
    WOLFSSL_METHOD* method = NULL;

    ret = wolfSSL_Init();
    if (ret != WOLFSSL_SUCCESS) {
        Serial.println("[TLS]: wolfSSL_Init failed");
        return -1;
    }

    // Поддержка и TLS 1.2, и TLS 1.3
    method = wolfSSLv23_client_method();
    if (method == NULL) {
        Serial.println("[TLS]: Unable to get client method");
        return -1;
    }

    if (ctx != NULL) {
        wolfSSL_CTX_free(ctx);
        ctx = NULL;
    }

    ctx = wolfSSL_CTX_new(method);
    if (ctx == NULL) {
        Serial.println("[TLS]: Unable to create CTX");
        return -1;
    }
    return WOLFSSL_SUCCESS;
}

int setup_certificates(void) {
    int ret = 0;

    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);

    // Per-device identity (P1b): генерируется один раз и переиспользуется из NVS.
    // Fallback на статический сертифицированный массив, если генерация недоступна.
    if (!deviceIdentity.isReady()) {
        deviceIdentity.ensure("Alice TV Remote");
    }

    const uint8_t* certBuf;
    uint32_t       certLen;
    const uint8_t* keyBuf;
    uint32_t       keyLen;
    if (deviceIdentity.isReady()) {
        certBuf = deviceIdentity.certDer(); certLen = deviceIdentity.certLen();
        keyBuf  = deviceIdentity.keyDer();  keyLen  = deviceIdentity.keyLen();
    } else {
        Serial.println("[TLS]: DeviceIdentity недоступен, использую статичный сертификат");
        certBuf = client_cert_der;          certLen = client_cert_der_len;
        keyBuf  = client_key_der;           keyLen  = client_key_der_len;
    }

    ret = wolfSSL_CTX_use_certificate_buffer(ctx, certBuf, certLen, WOLFSSL_FILETYPE_ASN1);
    if (ret != WOLFSSL_SUCCESS) {
        Serial.println("[TLS]: wolfSSL_CTX_use_certificate_buffer failed");
        wc_ErrorString(ret, wc_error_message);
        Serial.println(wc_error_message);
        return -1;
    }

    ret = wolfSSL_CTX_use_PrivateKey_buffer(ctx, keyBuf, keyLen, WOLFSSL_FILETYPE_ASN1);
    if (ret != WOLFSSL_SUCCESS) {
        Serial.println("[TLS]: wolfSSL_CTX_use_PrivateKey_buffer failed");
        wc_ErrorString(ret, wc_error_message);
        Serial.println(wc_error_message);
        return -1;
    }

    return WOLFSSL_SUCCESS;
}

int ssl_connect(IPAddress ip, uint16_t port) {
    if (client.connected()) {
        ssl_stop();
    }

    if (setup_wolfssl() < 0) {
        return -1;
    }

    if (setup_certificates() < 0) {
        return -1;
    }
    
    wolfSSL_SetIOSend(ctx, EthernetSend);
    wolfSSL_SetIORecv(ctx, EthernetReceive);

    Serial.printf("[TLS]: Подключение TCP к %s:%d...\n", ip.toString().c_str(), port);
    if (!client.connect(ip, port, 5000)) {
        Serial.printf("[TLS]: Ошибка TCP подключения к %s:%d\n", ip.toString().c_str(), port);
        return -1;
    }
    Serial.printf("[TLS]: TCP соединение установлено с %s:%d\n", ip.toString().c_str(), port);

    ssl = wolfSSL_new(ctx);
    if (ssl == NULL) {
        Serial.println("[TLS]: Ошибка wolfSSL_new");
        client.stop();
        return -1;
    }

    Serial.println("[TLS]: Начало TLS рукопожатия...");
    int ret = 0;
    int err = 0;
    int tries = 0;
    do {
        ret = wolfSSL_connect(ssl);
        if (ret == WOLFSSL_SUCCESS) {
            break;
        }
        err = wolfSSL_get_error(ssl, ret);
        if (err == WC_PENDING_E || 
            err == WOLFSSL_ERROR_WANT_READ || 
            err == WOLFSSL_ERROR_WANT_WRITE || 
            err == WOLFSSL_CBIO_ERR_WANT_READ ||
            err == 2) {
            delay(50);
            tries++;
        } else {
            Serial.printf("[TLS]: wolfSSL_connect ошибка: %d (%s)\n", err, wc_GetErrorString(err));
            break;
        }
    } while (tries < 100);

    if (ret != WOLFSSL_SUCCESS) {
        Serial.printf("[TLS]: Рукопожатие не удалось! (код: %d)\n", err);
        ssl_stop();
        return -1;
    }

    Serial.println("[TLS]: Рукопожатие успешно завершено!");
    return 1;
}

uint8_t ssl_send(const char* msg, int msgSz) {
    if (ssl == NULL) return 0;
    int ret = wolfSSL_write(ssl, msg, msgSz);
    return ret;
}

int ssl_available() {
    return client.available();
}

int ssl_read(char* reply, int sz) {
    if (ssl == NULL) return -1;
    return wolfSSL_read(ssl, reply, sz);
}

void ssl_stop() {
    if (ssl != NULL) {
        wolfSSL_free(ssl);
        ssl = NULL;
    }
    if (ctx != NULL) {
        wolfSSL_CTX_free(ctx);
        ctx = NULL;
    }
    client.stop();
}

uint8_t ssl_connected() {
    return (ssl != NULL && client.connected());
}

WOLFSSL_X509 *ssl_get_peer_certificate() {
    if (ssl == NULL) return NULL;
    return wolfSSL_get_peer_certificate(ssl);
}

WOLFSSL_X509* ssl_get_certificate() {
    if (ssl == NULL) return NULL;
    return wolfSSL_get_certificate(ssl);
}