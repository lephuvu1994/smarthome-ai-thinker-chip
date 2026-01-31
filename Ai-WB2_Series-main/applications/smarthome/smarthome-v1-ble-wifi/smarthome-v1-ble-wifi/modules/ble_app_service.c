#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include "bluetooth.h"
#include "gatt.h"
#include "ble_lib_api.h"

// Include các logic ứng dụng
#include "app_storage.h"
#include "app_led.h"
#include "app_conf.h"

/* ĐỊNH NGHĨA UUID VÀ SERVICE CỦA RIÊNG BẠN */
#define UUID1_USER_SER    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x55535343, 0xfe7d, 0x4ae5, 0x8fa9, 0x9fafd205e455))
#define UUID1_USER_TXD    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x49535343, 0x8841, 0x43f4, 0xa8d4, 0xecbe34729bb3))
#define UUID1_USER_RXD    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x49535343, 0x1e4d, 0x4bd9, 0xba61, 0x23c647249616))

#define SALVE_CMD_SERVER_TX_INDEX 2

// --- CALLBACK XỬ LÝ DỮ LIỆU TỪ ĐIỆN THOẠI GỬI XUỐNG ---
static ssize_t ble_wifi_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
             const void *buf, u16_t len, u16_t offset, u8_t flags)
{
    static char data_str[128];
    if (len > 127) len = 127;
    memcpy(data_str, buf, len);
    data_str[len] = '\0';

    printf("[BLE-APP] Recv Data: %s\r\n", data_str);

    // Logic tách chuỗi Wifi
    char *comma = strchr(data_str, ',');
    if(comma) {
        *comma = '\0';
        // Gọi module Storage để lưu và reboot
        save_wifi_and_reboot(data_str, comma+1);
    }

    return len;
}

static void ble_ccc_cfg_changed(const struct bt_gatt_attr *attr, u16_t value) {
    // Xử lý khi user đăng ký nhận Notify (nếu cần)
}

// --- BẢNG GATT (GATT TABLE) ---
static struct bt_gatt_attr my_gatt_attrs[]= {
    /* Primary Service */
    BT_GATT_PRIMARY_SERVICE(UUID1_USER_SER),

    /* TX: Gửi lên App */
    BT_GATT_CHARACTERISTIC(UUID1_USER_TXD, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ, NULL, NULL, NULL),
    BT_GATT_CCC(ble_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    /* RX: Nhận từ App (Quan trọng nhất) */
    BT_GATT_CHARACTERISTIC(UUID1_USER_RXD, BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE, NULL, ble_wifi_write_cb, NULL),
};

static struct bt_gatt_service my_gatt_service = BT_GATT_SERVICE(my_gatt_attrs);

// --- HÀM PUBLIC ---
int ble_app_service_init(void)
{
    // Đăng ký Service này vào hệ thống BLE Core
    int ret = bt_gatt_service_register(&my_gatt_service);
    if (ret == 0) {
        printf("[BLE-APP] Custom Service Registered.\r\n");
    }
    return ret;
}
