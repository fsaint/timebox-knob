#pragma once
// Copy this file to secrets.h (git-ignored) and fill it in to enable the Wi-Fi alert service.
// Without a secrets.h the sketch builds as a plain offline timer.

#define WIFI_SSID "your-network-name"   // 2.4 GHz network
#define WIFI_PASS "your-network-password"

// Requests must carry this value in an "X-Token" header. Leave empty ("") to disable the check.
#define ALERT_TOKEN "change-me"

// Name on the network: http://timebox.local
#define NET_HOSTNAME "timebox"

// POSIX timezone, used for "HH:MM" times in schedules. Example below: US Eastern.
#define TZ_INFO "EST5EDT,M3.2.0,M11.1.0"
