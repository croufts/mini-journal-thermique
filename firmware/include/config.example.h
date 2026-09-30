#pragma once

// Copy to journal_config.h, which is ignored by Git. No API key belongs on the ESP32.
#define WIFI_SSID "TON_WIFI_2_4_GHZ"
#define WIFI_PASSWORD "TON_MOT_DE_PASSE"
#define JOURNAL_BASE_URL "https://raw.githubusercontent.com/croufts/mini-journal-thermique/refs/heads/journal/"

// Recommended: exact Bluetooth Classic MAC, read with the serial SCAN command.
#define PRINTER_MAC "AA:BB:CC:DD:EE:FF"
// Leave MAC empty to discover/connect by exact name (slower).
#define PRINTER_NAME "M02 Pro"
#define BT_REQUIRE_PIN false
#define BT_PIN "0000"
#define BT_CHANNEL 1

#define PARIS_TZ "CET-1CEST,M3.5.0,M10.5.0/3"
#define HTTP_POLL_MS 300000UL
#define BT_POLL_MS 20000UL
#define BT_CHUNK_BYTES 128
#define BT_CHUNK_DELAY_MS 10
#define BT_FINISH_DELAY_MS 5000UL

// 0: native density. DENSITY 1..4 is available for controlled serial calibration.
#define BT_PRINT_DENSITY 0
