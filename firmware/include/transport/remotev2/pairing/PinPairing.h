#ifndef PIN_PAIRING_H
#define PIN_PAIRING_H

// State-машина PIN-сопряжения Google TV Remote Protocol v2 (передача "SPAM" —
// Self-generated PIN, Alphanumeric, Manual; PIN показывается на экране ТВ).
//
// Канонический порядок обмен PairingMessage (клиент=пульт, сервер=ТВ):
//   1. -> pairing_request           (PairingRequest)
//   2. <- pairing_ack               (PairingRequestAck)
//   3. -> pairing_option_request    (PairingOption)
//   4. <- pairing_option_response   (PairingOption)
//   5. -> pairing_config_request    (PairingConfiguration)
//   6. <- pairing_config_response   (PairingConfigurationAck)   // ТВ показывает PIN
//   7. -> pairing_secret_request    (PairingSecret: 32-байтный sha256-secret)
//   8. <- pairing_secret_response   (PairingSecretAck)          // сопряжение завершено
//
// Машина НАМЕРЕННО не знает ни о сокетах, ни о protobuf-c, ни о wolfSSL — она оперирует
// логическими типами сообщений и выводит pairing-secret через PairingSecret.h. Транспортный
// слой (P2) отображает protobuf-oneof <-> PinIncoming и PinOutgoing <-> байты на проводе.
// Такой разрыв позволяет полному пути пройти в нативном юнит-тесте на контрольном векторе.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "transport/remotev2/crypto/PairingSecret.h"

enum class PinState {
    Idle,             // ещё не запущена
    AwaitRequestAck,  // отправили PairingRequest, ждём PairingRequestAck
    AwaitOption,      // отправили PairingOption, ждём ответ PairingOption
    AwaitConfigAck,   // отправили PairingConfiguration, ждём PairingConfigurationAck
    AwaitPin,         // конфиг подтверждён, ТВ показал PIN — ждём ввод пользователя
    AwaitSecretAck,   // отправили PairingSecret, ждём PairingSecretAck
    Paired,           // успех
    Failed            // ошибка (см. failDetail())
};

// Что транспорт должен отправить прямо сейчас (None — ничего, ждём входящее).
enum class PinOutgoing { None, Request, Option, Config, Secret };

// Какой тип сообщения от сервера только что декодирован.
enum class PinIncoming { None, RequestAck, Option, ConfigAck, SecretAck, Error };

class PinPairing {
public:
    void reset() {
        _state = PinState::Idle;
        _outgoing = PinOutgoing::None;
        _hasSecret = false;
        _failDetail = nullptr;
    }

    // Начать сопряжение: ставим в очередь отправку PairingRequest.
    void start() {
        _state = PinState::AwaitRequestAck;
        _outgoing = PinOutgoing::Request;
        _hasSecret = false;
        _failDetail = nullptr;
    }

    PinState state() const { return _state; }
    PinOutgoing outgoing() const { return _outgoing; }
    bool needsPin() const { return _state == PinState::AwaitPin; }
    bool isPaired() const { return _state == PinState::Paired; }
    bool isFailed() const { return _state == PinState::Failed; }
    bool isDone() const { return _state == PinState::Paired || _state == PinState::Failed; }
    bool hasSecret() const { return _hasSecret; }
    const uint8_t* secret() const { return _secret; }
    const char* failDetail() const { return _failDetail; }

    // Транспорт известил, что текущее исходящее сообщение реально ушло в сокет.
    void markSent() {
        if (_outgoing != PinOutgoing::None) _outgoing = PinOutgoing::None;
    }

    // Скормить машине тип только что принятого серверного сообщения. Продвигает стейт и,
    // при корректной последовательности, ставит следующее исходящее сообщение.
    void onIncoming(PinIncoming in) {
        if (in == PinIncoming::Error) { fail("сервер вернул статусную ошибку сопряжения"); return; }
        switch (_state) {
            case PinState::AwaitRequestAck:
                if (in == PinIncoming::RequestAck) { _state = PinState::AwaitOption; _outgoing = PinOutgoing::Option; }
                else fail("ожидали PairingRequestAck");
                break;
            case PinState::AwaitOption:
                if (in == PinIncoming::Option) { _state = PinState::AwaitConfigAck; _outgoing = PinOutgoing::Config; }
                else fail("ожидали PairingOption");
                break;
            case PinState::AwaitConfigAck:
                if (in == PinIncoming::ConfigAck) { _state = PinState::AwaitPin; _outgoing = PinOutgoing::None; }
                else fail("ожидали PairingConfigurationAck");
                break;
            case PinState::AwaitSecretAck:
                if (in == PinIncoming::SecretAck) { _state = PinState::Paired; _outgoing = PinOutgoing::None; }
                else fail("ожидали PairingSecretAck");
                break;
            default:
                fail("неожиданное сообщение в текущем состоянии");
                break;
        }
    }

    // Пользователь ввёл 6 hex-символов PIN с экрана ТВ. Выводим pairing-secret по
    // эталонной формуле (нужны RSA-материалы клиента и сервера) и ставим в очередь
    // PairingSecret. Возвращает false, если PIN не прошёл контрольный байт или стейт
    // не AwaitPin (тогда машина переходит в Failed при несовпадении PIN).
    bool providePin(const char* pin6,
                    const uint8_t* clientMod, size_t clientModLen,
                    const uint8_t* clientExp, size_t clientExpLen,
                    const uint8_t* serverMod, size_t serverModLen,
                    const uint8_t* serverExp, size_t serverExpLen) {
        if (_state != PinState::AwaitPin) { fail("PIN предоставлен не на стадии ожидания PIN"); return false; }
        uint8_t sec[32];
        int ok = derivePairingSecret(clientMod, clientModLen, clientExp, clientExpLen,
                                     serverMod, serverModLen, serverExp, serverExpLen, pin6, sec);
        if (!ok) { fail("контрольный байт PIN не совпал — неверный PIN"); return false; }
        memcpy(_secret, sec, 32);
        _hasSecret = true;
        _state = PinState::AwaitSecretAck;
        _outgoing = PinOutgoing::Secret;
        return true;
    }

private:
    void fail(const char* detail) {
        _state = PinState::Failed;
        _outgoing = PinOutgoing::None;
        _failDetail = detail;
    }

    PinState _state = PinState::Idle;
    PinOutgoing _outgoing = PinOutgoing::None;
    uint8_t _secret[32] = {0};
    bool _hasSecret = false;
    const char* _failDetail = nullptr;
};

#endif // PIN_PAIRING_H
