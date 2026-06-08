#include "gw_id.h"
#include <stdio.h>
#include <esp_mac.h>
#include <esp_log.h>

static char gw_id_str[32];

void gw_id_init(void)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(gw_id_str, sizeof(gw_id_str), "seeedmote-gw-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
}

const char *gw_id_get(void)
{
    return gw_id_str;
}
