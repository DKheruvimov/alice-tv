// Шаблон дев-дефолтов для отладочного стенда.
// Скопировать в credentials.h (тот игнорируется git) и заполнить своими значениями.
// ВНИМАНИЕ: продакшн-устройство получает эти параметры из NVS через портал /setup,
// а НЕ из этого файла. Реальный credentials.h в репозиторий не коммитится.
#pragma once

#define DEV_WIFI_SSID "YourNetworkSSID"
#define DEV_WIFI_PASS "YourNetworkPassword"
#define DEV_TV_IP 192, 168, 0, 1
