#include "app_conf.h"
#include <easyflash.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

app_config_t g_cfg;

void storage_init() {
    easyflash_init();
}

void storage_load_all() {
    // Xóa trắng struct trước khi load
    memset(&g_cfg, 0, sizeof(app_config_t));

    // Đọc Wifi
    ef_get_env_blob("wifi_ssid", g_cfg.ssid, 32, NULL);
    ef_get_env_blob("wifi_pass", g_cfg.pass, 64, NULL);

    // Đọc Server Config
    ef_get_env_blob("srv_api", g_cfg.api_host, 63, NULL);
    ef_get_env_blob("srv_mqtt", g_cfg.mqtt_host, 63, NULL);

    // Đọc Identity
    char stt_buf[16] = {0};
    ef_get_env_blob("dev_stt", stt_buf, 15, NULL);
    if(strlen(stt_buf) > 0) g_cfg.device_stt = atoi(stt_buf);

    ef_get_env_blob("dev_token", g_cfg.device_token, 64, NULL);

    char active_buf[4] = {0};
    ef_get_env_blob("is_active", active_buf, 3, NULL);
    g_cfg.is_activated = (active_buf[0] == '1') ? 1 : 0;

    printf("[STORAGE] Loaded: SSID=%s, Active=%d, STT=%d\r\n",
           g_cfg.ssid, g_cfg.is_activated, g_cfg.device_stt);
}

void storage_save_wifi(char* ssid, char* pass) {
    ef_set_env_blob("wifi_ssid", ssid, strlen(ssid));
    ef_set_env_blob("wifi_pass", pass, strlen(pass));
    ef_save_env();
}

void storage_save_bootstrap(char* api, char* mqtt, int port) {
    ef_set_env_blob("srv_api", api, strlen(api));
    ef_set_env_blob("srv_mqtt", mqtt, strlen(mqtt));
    // Port lưu tạm chưa cần thiết flash nếu fix cứng 1883
    ef_save_env();
}

void storage_save_identity(int stt, char* token) {
    char buf[16];
    sprintf(buf, "%d", stt);
    ef_set_env_blob("dev_stt", buf, strlen(buf));
    ef_set_env_blob("dev_token", token, strlen(token));
    ef_set_env_blob("is_active", "1", 1);
    ef_save_env();
}

void storage_factory_reset() {
    printf("[STORAGE] Wiping all data...\r\n");
    ef_env_set_default();
    ef_save_env();
}
