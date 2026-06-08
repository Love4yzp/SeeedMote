#include "cli.h"
#include "gw_id.h"
#include "nvs_config.h"
#include "wifi_mgr.h"
#include "mqtt_mgr.h"
#include "led.h"
#include <string.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_console.h>
#include <esp_chip_info.h>
#include <esp_timer.h>
#include <nvs_flash.h>

static int cmd_info(int argc, char **argv)
{
    printf("Gateway:  %s\n", gw_id_get());
    printf("Firmware: %s\n", GATEWAY_FW_VERSION);
    int64_t us = esp_timer_get_time();
    int sec = (int)(us / 1000000);
    printf("Uptime:   %dd %dh %dm %ds\n",
           sec / 86400, (sec % 86400) / 3600, (sec % 3600) / 60, sec % 60);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("Chip:     ESP32-S3 rev %d.%d, %d core(s)\n",
           chip.revision / 100, chip.revision % 100, chip.cores);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    struct gw_config cfg;
    nvs_config_load(&cfg);

    printf("WiFi:  %s", wifi_mgr_is_connected() ? "connected" : "disconnected");
    if (wifi_mgr_is_connected())
        printf("  IP=%s  SSID=%s", wifi_mgr_get_ip(), cfg.wifi_ssid);
    printf("\n");

    printf("MQTT:  %s", mqtt_mgr_is_connected() ? "connected" : "disconnected");
    if (cfg.has_mqtt)
        printf("  broker=%s:%u", cfg.mqtt_broker, cfg.mqtt_port);
    printf("\n");

    return 0;
}

static int cmd_wifi_scan(int argc, char **argv)
{
    printf("Scanning...\n");
    cJSON *arr = wifi_mgr_scan();
    int n = cJSON_GetArraySize(arr);
    if (n == 0) {
        printf("No networks found.\n");
    } else {
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(arr, i);
            printf("  %-32s  %4.0f dBm  %s\n",
                   cJSON_GetStringValue(cJSON_GetObjectItem(item, "ssid")),
                   cJSON_GetObjectItem(item, "rssi")->valuedouble,
                   cJSON_GetStringValue(cJSON_GetObjectItem(item, "auth")));
        }
    }
    cJSON_Delete(arr);
    return 0;
}

static int cmd_wifi_connect(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: wifi_connect <ssid> [password]\n");
        return 1;
    }
    const char *ssid = argv[1];
    const char *pass = argc > 2 ? argv[2] : "";
    nvs_config_save_wifi(ssid, pass);
    wifi_mgr_connect(ssid, pass);
    printf("Connecting to %s...\n", ssid);
    return 0;
}

static int cmd_mqtt_connect(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: mqtt_connect <broker> [port] [password]\n");
        return 1;
    }
    const char *broker = argv[1];
    uint16_t port = 1883;
    const char *pass = "";
    if (argc > 2) port = (uint16_t)atoi(argv[2]);
    if (argc > 3) pass = argv[3];

    nvs_config_save_mqtt(broker, port, pass);
    mqtt_mgr_reconfigure(broker, port, gw_id_get(), pass);
    printf("MQTT -> %s:%u\n", broker, port);
    return 0;
}

static int cmd_nvs_show(int argc, char **argv)
{
    struct gw_config cfg;
    nvs_config_load(&cfg);
    printf("wifi_ssid:    %s\n", cfg.has_wifi ? cfg.wifi_ssid : "(not set)");
    printf("wifi_pass:    %s\n", cfg.has_wifi ? cfg.wifi_pass : "(not set)");
    printf("mqtt_broker:  %s\n", cfg.has_mqtt ? cfg.mqtt_broker : "(not set)");
    printf("mqtt_port:    %u\n", cfg.mqtt_port);
    printf("mqtt_pass:    %s\n", cfg.has_mqtt ? cfg.mqtt_pass : "(not set)");
    return 0;
}

static int cmd_nvs_clear(int argc, char **argv)
{
    nvs_handle_t h;
    if (nvs_open("gw_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    printf("NVS cleared. Restart to apply.\n");
    return 0;
}

static int cmd_locate(int argc, char **argv)
{
    led_locate_blink();
    printf("Blinking LED.\n");
    return 0;
}

static int cmd_restart(int argc, char **argv)
{
    printf("Restarting...\n");
    esp_restart();
    return 0;
}

void cli_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "gw> ";
    repl_cfg.max_cmdline_length = 256;

    esp_console_dev_usb_serial_jtag_config_t hw_cfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    const esp_console_cmd_t cmds[] = {
        { .command = "info",         .help = "Gateway ID, firmware version, uptime", .func = cmd_info },
        { .command = "status",       .help = "WiFi and MQTT connection status",      .func = cmd_status },
        { .command = "wifi_scan",    .help = "Scan WiFi networks",                   .func = cmd_wifi_scan },
        { .command = "wifi_connect", .help = "wifi_connect <ssid> [password]",       .func = cmd_wifi_connect },
        { .command = "mqtt_connect", .help = "mqtt_connect <broker> [port] [pass]",  .func = cmd_mqtt_connect },
        { .command = "nvs_show",     .help = "Show saved config",                    .func = cmd_nvs_show },
        { .command = "nvs_clear",    .help = "Erase all saved config (factory reset)", .func = cmd_nvs_clear },
        { .command = "locate",       .help = "Blink the LED",                        .func = cmd_locate },
        { .command = "restart",      .help = "Restart gateway",                      .func = cmd_restart },
    };

    esp_console_new_repl_usb_serial_jtag(&hw_cfg, &repl_cfg, &repl);

    for (int i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); i++) {
        esp_console_cmd_register(&cmds[i]);
    }

    esp_console_start_repl(repl);
}
