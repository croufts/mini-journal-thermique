#pragma once

// Copy to journal_config.h, which is ignored by Git. No API key belongs on the ESP32.
#define WIFI_SSID "TON_WIFI_2_4_GHZ"
#define WIFI_PASSWORD "TON_MOT_DE_PASSE"
#define JOURNAL_BASE_URL "https://raw.githubusercontent.com/TON_COMPTE/mini-journal-thermique/refs/heads/journal/"

// Exact Bluetooth Classic MAC, read with the serial SCAN command.
#define PRINTER_MAC ""
// Leave MAC empty to discover/connect by exact name (slower).
#define PRINTER_NAME "M02 Pro"
// Classic SPP security settings. Channel is discovered automatically.
#define BT_REQUIRE_PIN false
#define BT_PIN "0000"

#define PARIS_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
// Local cycle starts at 05:00 Paris time, including daylight saving changes.
#define DAILY_START_HOUR 5
// Optional fine-grained GitHub token: only this repository, Actions read/write.
// Kept solely in ignored journal_config.h; never in the public repository.
#define GITHUB_ACTIONS_TOKEN ""
#define HTTP_POLL_MS 300000UL
#define BT_POLL_MS 20000UL

// 0: omit override. Density 4 retained after hardware comparison.
#define BT_PRINT_DENSITY 4
