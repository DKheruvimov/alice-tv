#ifndef ITV_TRANSPORT_H
#define ITV_TRANSPORT_H

#include <Arduino.h>
#include <WiFiType.h>

// Результат команды транспорта: успех + человекочитаемая диагностика (вывод/ошибка).
struct TvResult {
    bool ok = false;
    String detail;
};

// Единый интерфейс транспорта управления телевизором (AGENTS.md 2.2).
// Смена реализации (ADB over TCP <-> Google TV Remote Protocol v2) не должна
// затрагивать MQTT-контракт, серверный API и логику интеграции с Алисой.
// Методы соответствуют действиям контракта launch/key/text/seek (AGENTS.md 4.2).
class ITvTransport {
public:
    virtual ~ITvTransport() {}

    // Установка соединения с телевизором.
    virtual bool begin(IPAddress ip, uint16_t port) = 0;
    virtual bool isConnected() = 0;

    // Идентификатор транспорта для диагностики/выбора: "adb" | "remote_v2".
    virtual const char* serviceType() = 0;

    // Готов ли транспорт к командам (для v2 — прошедший сопряжение и control-канал).
    virtual bool isReady() { return isConnected(); }

    // Требует ли транспорт сопряжения (v2 — да, ADB — нет).
    virtual bool needsPairing() { return false; }

    // Запуск приложения: target — логическое имя ("kinopoisk") ИЛИ имя пакета ("ru.kinopoisk.tv").
    virtual TvResult launch(const String& target) = 0;

    // Отправка клавиши по символическому имени ("play_pause", "vol_up", ...).
    virtual TvResult key(const String& name) = 0;

    // Ввод текста в активное поле ввода.
    virtual TvResult text(const String& value) = 0;

    // Голосовой / текстовый поиск на ТВ (эмуляция "Алиса, найди ...")
    virtual TvResult search(const String& query) { return text(query); }

    // Серия шагов перемотки: steps > 0 — вперёд, < 0 — назад.
    virtual TvResult seek(int steps) = 0;

    // Инвентарь запускаемых приложений: JSON-массив имён пакетов.
    virtual String listApps() = 0;
};

#endif
