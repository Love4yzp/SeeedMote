#pragma once

/* Current STA IP (zero-string until connected). */
extern char s_sta_ip[20];

/* AP SSID (seeedmote-gw-XXYY). */
extern char s_ap_ssid[24];

/* STA credentials loaded from NVS (or compiled-in defaults). */
extern char s_wifi_ssid[33];

void wifi_init(void);
