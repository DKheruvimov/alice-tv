#ifndef ADB_TRANSPORT_H
#define ADB_TRANSPORT_H

#include "transport/ITvTransport.h"
#include "AdbClient.h"

// Реализация ITvTransport поверх ADB over TCP (транспорт MVP, AGENTS.md 2.2).
class AdbTransport : public ITvTransport {
public:
    bool begin(IPAddress ip, uint16_t port) override;
    bool isConnected() override;
    const char* serviceType() override { return "adb"; }

    TvResult launch(const String& target) override;
    TvResult key(const String& name) override;
    TvResult text(const String& value) override;
    TvResult search(const String& query) override;
    TvResult seek(int steps) override;
    String listApps() override;

    // Диагностика веб-консоли: прямой shell-запрос (специфично для ADB).
    AdbShellResult rawShell(const String& cmd, uint32_t timeoutMs = 10000);

    AdbClient client;

private:
    // Логическое имя -> имя пакета; если target уже похож на пакет — вернуть как есть.
    String resolvePackage(const String& target);
};

#endif
