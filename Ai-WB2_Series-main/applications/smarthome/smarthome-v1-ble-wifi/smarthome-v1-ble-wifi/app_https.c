#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>

// --- LwIP & Network ---
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <wifi_mgmr_ext.h>

// --- mbedTLS (Security) ---
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

// --- JSON Library ---
#include <cJSON.h>

// --- APP MODULES ---
#include "app_conf.h"     // Chứa HTTPS_SERVER_DNS, HTTPS_PORT_STR...
#include "app_storage.h"  // Chứa hàm lưu Flash
#include "app_watchdog.h"

int app_https_register_device(void) {
    int ret = 0;

    // Khai báo các cấu trúc của mbedTLS
    mbedtls_net_context server_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;

    // 1. Cấp phát Buffer (Heap)
    char *buf = pvPortMalloc(1024);
    if (!buf) {
        printf("[HTTPS] Error: Out of Memory (OOM)!\r\n");
        return 0;
    }

    // 2. Lấy MAC Address
    uint8_t mac[6];
    wifi_mgmr_sta_mac_get(mac);
    char mac_str[18];
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    printf("[HTTPS] Connecting to Server: %s ...\r\n", HTTPS_SERVER_DNS);

    // 3. Khởi tạo mbedTLS
    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    // Seed bộ sinh số ngẫu nhiên
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"a", 1)) != 0) {
        printf("[HTTPS] Error: RNG failed\r\n"); goto exit;
    }

    // 4. Kết nối TCP (Dùng biến HTTPS_PORT_STR từ app_conf.h)
    if ((ret = mbedtls_net_connect(&server_fd, HTTPS_SERVER_DNS, HTTPS_PORT_STR, MBEDTLS_NET_PROTO_TCP)) != 0) {
        printf("[HTTPS] Error: Connect failed (-0x%x)\r\n", -ret); goto exit;
    }

    // 5. Cấu hình SSL (Client Mode)
    if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        printf("[HTTPS] Error: SSL config failed\r\n"); goto exit;
    }

    // Tắt xác thực chứng chỉ (Để đơn giản hóa và tiết kiệm RAM)
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_setup(&ssl, &conf);

    // Gán Socket vào SSL (Lưu ý tham số NULL ở cuối cho SDK BL602)
    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    // 6. Bắt tay (Handshake)
    printf("[HTTPS] Handshaking...\r\n");
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            printf("[HTTPS] Error: Handshake failed (-0x%x)\r\n", -ret); goto exit;
        }
    }
    printf("[HTTPS] Handshake Success! Sending Data...\r\n");

    // 7. Gửi Request
    int len = sprintf(buf,
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{\"mac\":\"%s\"}",
        HTTPS_URL, HTTPS_SERVER_DNS, (int)strlen(mac_str)+10, mac_str);

    if ((ret = mbedtls_ssl_write(&ssl, (const unsigned char *)buf, len)) <= 0) {
        printf("[HTTPS] Error: Write failed\r\n"); goto exit;
    }

    // 8. Nhận phản hồi
    printf("[HTTPS] Reading Response...\r\n");
    memset(buf, 0, 1024);

    // Đọc dữ liệu đã giải mã
    ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf, 1023);

    if (ret > 0) {
        buf[ret] = '\0'; // Đảm bảo null-terminated

        // --- LOG DEBUG ---
        printf("\r\n--- [HTTPS RESPONSE START] ---\r\n");
        printf("%s", buf);
        printf("\r\n--- [HTTPS RESPONSE END] ---\r\n");

        // 9. Xử lý dữ liệu với cJSON
        if (strstr(buf, "200 OK") || strstr(buf, "201 Created")) {

            // Tìm JSON Body (sau \r\n\r\n)
            char *json_body = strstr(buf, "\r\n\r\n");

            if(json_body) {
                json_body += 4;
                printf("[HTTPS] Parsing JSON Body: %s\r\n", json_body);

                cJSON *root = cJSON_Parse(json_body);
                if (root) {
                    cJSON *broker = cJSON_GetObjectItem(root, "broker");
                    cJSON *user   = cJSON_GetObjectItem(root, "user");
                    cJSON *pass   = cJSON_GetObjectItem(root, "pass");
                    cJSON *token  = cJSON_GetObjectItem(root, "token");

                    // [FIX QUAN TRỌNG] Thay cJSON_IsString bằng kiểm tra thủ công cho SDK cũ
                    if ((broker && broker->type == cJSON_String) &&
                        (token  && token->type == cJSON_String)) {

                        printf("[HTTPS] Registration Success! Saving...\r\n");

                        // Cũng phải kiểm tra user/pass để tránh lỗi truy cập bộ nhớ
                        storage_save_mqtt_info(broker->valuestring,
                                               (user && user->type == cJSON_String) ? user->valuestring : "",
                                               (pass && pass->type == cJSON_String) ? pass->valuestring : "",
                                               token->valuestring);
                        ret = 1; // Success
                    } else {
                        printf("[HTTPS] Error: Missing 'broker' or 'token' in JSON\r\n");
                    }
                    cJSON_Delete(root);
                } else {
                    printf("[HTTPS] Error: JSON Parse Failed\r\n");
                }
            }
        } else {
            printf("[HTTPS] Error: Server did not return 200 OK\r\n");
        }
    } else {
        printf("[HTTPS] Error: Empty response or Read Error (-0x%x)\r\n", -ret);
    }

exit:
    // Giải phóng tài nguyên mbedTLS
    mbedtls_net_free(&server_fd);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    vPortFree(buf); // Trả lại RAM Heap

    return (ret == 1);
}
