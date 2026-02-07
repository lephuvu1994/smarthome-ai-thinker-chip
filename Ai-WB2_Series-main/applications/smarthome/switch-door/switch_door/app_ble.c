#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <blog.h>

// --- BLE SDK ---
#include "ble_lib_api.h"
#include "conn.h"
#include "gatt.h"
#include "hci_driver.h"
#include "bluetooth.h"

// --- MODULES ---
#include <cJSON.h>
#include "app_storage.h"
#include "app_buzzer.h"
#include "app_ble.h"
#include "app_conf.h"

// --- FIX LỖI UUID ---
// BT_UUID_128_ENCODE takes 5 arguments: w32, w16, w16, w16, w48
// Original: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
#define UUID_SVC_DEF   BT_UUID_128_ENCODE(0x00010203, 0x0405, 0x0607, 0x0809, 0x0A0B0C0D0E0F)
#define UUID_RX_DEF    BT_UUID_128_ENCODE(0x10111213, 0x1415, 0x1617, 0x1819, 0x1A1B1C1D1E1F)

#define UUID_SVC      BT_UUID_DECLARE_128(UUID_SVC_DEF)
#define UUID_CHR_RX   BT_UUID_DECLARE_128(UUID_RX_DEF)

static int g_ble_active = 0;

static ssize_t ble_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, 
                            const void *buf, u16_t len, u16_t offset, u8_t flags) 
{
    static char data[512]; 
    if(len > 511) len = 511;
    memcpy(data, buf, len);
    data[len] = '\0';

    printf("[BLE] RX Payload: %s\r\n", data);
    
    cJSON *root = cJSON_Parse(data);
    if (!root) {
        printf("[BLE] JSON Parse Fail\r\n");
        return len;
    }

    cJSON *wifi_ssid = cJSON_GetObjectItem(root, "wifi_ssid");
    cJSON *wifi_pass = cJSON_GetObjectItem(root, "wifi_pass");
    cJSON *mqtt_broker = cJSON_GetObjectItem(root, "mqtt_broker");
    cJSON *mqtt_user = cJSON_GetObjectItem(root, "mqtt_username");
    cJSON *mqtt_pass = cJSON_GetObjectItem(root, "mqtt_pass");
    cJSON *mqtt_token = cJSON_GetObjectItem(root, "mqtt_token_device");

    if (mqtt_broker && mqtt_token) {
        storage_save_mqtt_info(mqtt_broker->valuestring, 
                               mqtt_user ? mqtt_user->valuestring : "", 
                               mqtt_pass ? mqtt_pass->valuestring : "", 
                               mqtt_token->valuestring);
        printf("[BLE] Saved MQTT Config.\r\n");
    }

    if (wifi_ssid && wifi_pass) {
        printf("[BLE] Saved Wifi Config -> Rebooting...\r\n");
        app_buzzer_beep(BUZZER_TIME_LONG); 
        vTaskDelay(pdMS_TO_TICKS(1000));
        storage_save_wifi_reboot(wifi_ssid->valuestring, wifi_pass->valuestring);
    }

    cJSON_Delete(root);
    return len;
}

static struct bt_gatt_attr config_attrs[]= {
    BT_GATT_PRIMARY_SERVICE(UUID_SVC),
    BT_GATT_CHARACTERISTIC(UUID_CHR_RX, 
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, 
                           BT_GATT_PERM_WRITE, 
                           NULL, ble_write_cb, NULL),
};

static struct bt_gatt_service config_service = BT_GATT_SERVICE(config_attrs);

static struct bt_data ad_config[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, BLE_DEV_NAME, 12), 
};

static void ble_init_callback(int err) {
    if (!err) {
        bt_gatt_service_register(&config_service);
        printf("[BLE] Stack Init Success. Service Registered.\r\n");
    } else {
        printf("[BLE] Stack Init Failed: %d\r\n", err);
    }
}

void app_ble_init(void) {
    ble_controller_init(configMAX_PRIORITIES - 2); 
    hci_driver_init();
    bt_enable(ble_init_callback);
}

void app_ble_start_adv(void) {
    if (g_ble_active) return;
    int err = bt_le_adv_start(BT_LE_ADV_CONN, ad_config, ARRAY_SIZE(ad_config), NULL, 0);
    if (err) {
        printf("[BLE] Start Adv Failed: %d\r\n", err);
    } else {
        g_ble_active = 1;
        printf("[BLE] Advertising Started (Name: %s)\r\n", BLE_DEV_NAME);
    }
}

void app_ble_stop_adv(void) {
    if (!g_ble_active) return;
    bt_le_adv_stop();
    g_ble_active = 0;
    printf("[BLE] Advertising Stopped.\r\n");
}

void app_ble_set_adv_data(const char* status_json) {
    if (!g_ble_active) return;
    bt_le_adv_stop();

    struct bt_data ad_new[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_NAME_COMPLETE, BLE_DEV_NAME, 12),
        BT_DATA(BT_DATA_MANUFACTURER_DATA, status_json, strlen(status_json)) 
    };

    bt_le_adv_start(BT_LE_ADV_CONN, ad_new, ARRAY_SIZE(ad_new), NULL, 0);
    printf("[BLE] Updated Adv Data: %s\r\n", status_json);
}