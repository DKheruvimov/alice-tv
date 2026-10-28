#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "transport/remotev2/crypto/PairingSecret.h"
#include "transport/remotev2/pairing/PinPairing.h"

// Пустые фикстуры: PlatformIO-сборка Unity на хосте ссылается на setUp/tearDown.
void setUp(void) {}
void tearDown(void) {}

// Канонический вектор SHA-256 ("abc") — self-тест встроенного хеша.
static void test_sha256_abc(void) {
    ps_detail::Sha256 s;
    ps_detail::sha256_init(&s);
    const uint8_t msg[3] = {'a', 'b', 'c'};
    ps_detail::sha256_update(&s, msg, 3);
    uint8_t out[32];
    ps_detail::sha256_final(&s, out);

    static const uint8_t expect[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    TEST_ASSERT_EQUAL_INT8_ARRAY(expect, out, 32);
}

// Контрольный вектор выводки pairing-secret, рассчитан по формуле эталона
// tronikos/androidtvremote2 pairing.py:
//   sha256( clientMod || clientExp || serverMod || serverExp || pin[2:] )
// clientMod = 128*0xC1, clientExp = 01 00 01, serverMod = 128*0xD2, serverExp = 01 00 01,
// PIN = "543456" (первые 2 hex = контрольный байт 0x54 == digest[0]).
static void test_pairing_secret_control_vector(void) {
    uint8_t clientMod[128]; memset(clientMod, 0xC1, sizeof(clientMod));
    uint8_t serverMod[128]; memset(serverMod, 0xD2, sizeof(serverMod));
    uint8_t exp[3] = {0x01, 0x00, 0x01};

    static const uint8_t expect[32] = {
        0x54,0x6B,0x6B,0xDB,0x60,0x7B,0x02,0xEA,0x09,0xDB,0xD7,0x91,0x3B,0x43,0xD8,0xEE,
        0x1D,0xD1,0x1D,0x39,0xED,0x3C,0x03,0xF7,0xCD,0xBE,0x8E,0x2B,0xE9,0x52,0x73,0xCF
    };

    uint8_t out[32];
    int ok = derivePairingSecret(clientMod, sizeof(clientMod), exp, sizeof(exp),
                                 serverMod, sizeof(serverMod), exp, sizeof(exp),
                                 "543456", out);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, ok, "checksum-валидация корректного PIN должна пройти");
    TEST_ASSERT_EQUAL_INT8_ARRAY(expect, out, 32);
}

// Корректные mod/exp/PIN, но ЗАВЕДОМО НЕВЕРНЫЙ контрольный байт PIN -> ok==0,
// при этом хеш всё равно детерминирован (совпадает с предыдущим вектором, т.к.
// в хеш входят только pin[2:]="3456", а pin[0:2] участвует лишь в проверке).
static void test_pairing_secret_bad_pin_checksum(void) {
    uint8_t clientMod[128]; memset(clientMod, 0xC1, sizeof(clientMod));
    uint8_t serverMod[128]; memset(serverMod, 0xD2, sizeof(serverMod));
    uint8_t exp[3] = {0x01, 0x00, 0x01};

    static const uint8_t expect[32] = {
        0x54,0x6B,0x6B,0xDB,0x60,0x7B,0x02,0xEA,0x09,0xDB,0xD7,0x91,0x3B,0x43,0xD8,0xEE,
        0x1D,0xD1,0x1D,0x39,0xED,0x3C,0x03,0xF7,0xCD,0xBE,0x8E,0x2B,0xE9,0x52,0x73,0xCF
    };

    uint8_t out[32];
    // pin[0:2] = "FF" != digest[0] (0x54) -> false, но pin[2:] = "3456" тот же.
    int ok = derivePairingSecret(clientMod, sizeof(clientMod), exp, sizeof(exp),
                                 serverMod, sizeof(serverMod), exp, sizeof(exp),
                                 "FF3456", out);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ok, "несовпадение контрольного байта должно отклонить PIN");
    TEST_ASSERT_EQUAL_INT8_ARRAY(expect, out, 32);
}

// Некорректный ввод (длина PIN != 6) -> отказ без записи.
static void test_pairing_secret_rejects_malformed_pin(void) {
    uint8_t clientMod[4] = {1,2,3,4};
    uint8_t exp[1] = {3};
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(0, derivePairingSecret(clientMod, 4, exp, 1, clientMod, 4, exp, 1, "12345", out));
    TEST_ASSERT_EQUAL_INT(0, derivePairingSecret(clientMod, 4, exp, 1, clientMod, 4, exp, 1, "12345G", out));
}

// Полный успешный путь стейт-машины PinPairing на контрольном векторе:
// Request->Ack->Option->OptionResp->Config->ConfigAck->PIN->Secret->SecretAck.
static void test_pin_pairing_happy_path(void) {
    uint8_t clientMod[128]; memset(clientMod, 0xC1, sizeof(clientMod));
    uint8_t serverMod[128]; memset(serverMod, 0xD2, sizeof(serverMod));
    uint8_t exp[3] = {0x01, 0x00, 0x01};
    static const uint8_t expectSecret[32] = {
        0x54,0x6B,0x6B,0xDB,0x60,0x7B,0x02,0xEA,0x09,0xDB,0xD7,0x91,0x3B,0x43,0xD8,0xEE,
        0x1D,0xD1,0x1D,0x39,0xED,0x3C,0x03,0xF7,0xCD,0xBE,0x8E,0x2B,0xE9,0x52,0x73,0xCF
    };

    PinPairing p;
    p.start();
    TEST_ASSERT_TRUE(p.outgoing() == PinOutgoing::Request);
    p.markSent();

    p.onIncoming(PinIncoming::RequestAck);
    TEST_ASSERT_TRUE(p.outgoing() == PinOutgoing::Option);
    p.markSent();

    p.onIncoming(PinIncoming::Option);
    TEST_ASSERT_TRUE(p.outgoing() == PinOutgoing::Config);
    p.markSent();

    p.onIncoming(PinIncoming::ConfigAck);
    TEST_ASSERT_TRUE_MESSAGE(p.needsPin(), "после ConfigAck машина должна ждать PIN");
    TEST_ASSERT_TRUE(p.outgoing() == PinOutgoing::None);

    bool ok = p.providePin("543456",
                           clientMod, sizeof(clientMod), exp, sizeof(exp),
                           serverMod, sizeof(serverMod), exp, sizeof(exp));
    TEST_ASSERT_TRUE_MESSAGE(ok, "корректный PIN должен принять secret");
    TEST_ASSERT_TRUE(p.hasSecret());
    TEST_ASSERT_TRUE(p.outgoing() == PinOutgoing::Secret);
    TEST_ASSERT_EQUAL_INT8_ARRAY(expectSecret, p.secret(), 32);
    p.markSent();

    p.onIncoming(PinIncoming::SecretAck);
    TEST_ASSERT_TRUE_MESSAGE(p.isPaired(), "SecretAck завершает сопряжение");
    TEST_ASSERT_TRUE(p.isDone());
}

// Дошли до ожидания PIN, но контрольный байт неверный -> providePin false, машина в Failed.
static void test_pin_pairing_bad_pin_fails(void) {
    uint8_t clientMod[128]; memset(clientMod, 0xC1, sizeof(clientMod));
    uint8_t serverMod[128]; memset(serverMod, 0xD2, sizeof(serverMod));
    uint8_t exp[3] = {0x01, 0x00, 0x01};

    PinPairing p;
    p.start();
    p.onIncoming(PinIncoming::RequestAck);
    p.onIncoming(PinIncoming::Option);
    p.onIncoming(PinIncoming::ConfigAck);
    TEST_ASSERT_TRUE(p.needsPin());

    bool ok = p.providePin("FF3456",  // pin[0:2]=FF != digest[0]=0x54
                           clientMod, sizeof(clientMod), exp, sizeof(exp),
                           serverMod, sizeof(serverMod), exp, sizeof(exp));
    TEST_ASSERT_FALSE_MESSAGE(ok, "несовпадение контрольного байта отвергается");
    TEST_ASSERT_TRUE(p.isFailed());
    TEST_ASSERT_FALSE(p.hasSecret());
}

// Нарушение порядка сообщений и поздний PIN должны приводить к отказу, а не к зависанию.
static void test_pin_pairing_out_of_order_fails(void) {
    uint8_t clientMod[4] = {1,2,3,4};
    uint8_t exp[1] = {3};

    // Неверный тип в AwaitRequestAck (ждём RequestAck, приходит Option).
    PinPairing a;
    a.start();
    a.onIncoming(PinIncoming::Option);
    TEST_ASSERT_TRUE_MESSAGE(a.isFailed(), "сообщение не по порядку -> Failed");

    // PIN раньше времени (машина ещё не в AwaitPin).
    PinPairing b;
    b.start();
    bool ok = b.providePin("543456", clientMod, 4, exp, 1, clientMod, 4, exp, 1);
    TEST_ASSERT_FALSE_MESSAGE(ok, "PIN до ConfigAck не принимается");

    // Серверная статусная ошибка в любом состоянии -> Failed.
    PinPairing c;
    c.start();
    c.onIncoming(PinIncoming::Error);
    TEST_ASSERT_TRUE(c.isFailed());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_pairing_secret_control_vector);
    RUN_TEST(test_pairing_secret_bad_pin_checksum);
    RUN_TEST(test_pairing_secret_rejects_malformed_pin);
    RUN_TEST(test_pin_pairing_happy_path);
    RUN_TEST(test_pin_pairing_bad_pin_fails);
    RUN_TEST(test_pin_pairing_out_of_order_fails);
    return UNITY_END();
}
