#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>

// --- LwIP & Network ---
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/ip_addr.h>
#include <wifi_mgmr_ext.h>

// --- JSON Library ---
#include <cJSON.h>

// --- APP MODULES ---
#include "app_conf.h"
#include "app_storage.h"

int app_http_register_device(void) {
    int ret = 0;
    int sock = -1;
    struct sockaddr_in server_addr;

    // 1. Cấp phát bộ nhớ đệm
    char *buf = pvPortMalloc(1024);
    if (!buf) {
        printf("[HTTP] Error: Out of Memory (OOM)!\r\n");
        return 0;
    }

    // 2. Lấy MAC
    uint8_t mac[6];
    wifi_mgmr_sta_mac_get(mac);
    char mac_str[18];
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    printf("[HTTP] Connecting to Server: %s : %d ...\r\n", HTTP_SERVER_IP, HTTP_PORT);

    // 3. Tạo Socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("[HTTP] Error: Create socket failed\r\n");
        goto exit;
    }

    // 4. Cấu hình địa chỉ
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(HTTP_PORT);
    server_addr.sin_addr.s_addr = inet_addr(HTTP_SERVER_IP);

       // 5. Kết nối
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            printf("[HTTP] Error: Connect failed. Check IP/Firewall.\r\n");
            goto exit;
        }
        printf("[HTTP] Connected!\r\n");

        // ============================================================
        // [LOGIC MỚI] Tạo JSON Body trước để tính Content-Length
        // ============================================================
        char json_body[256];
        memset(json_body, 0, sizeof(json_body));

        // Format đúng kiểu JSON: {"mac_address": "...", "device_code": "..."}
        snprintf(json_body, sizeof(json_body),
                 "{\"mac_address\":\"%s\",\"device_code\":\"%s\"}",
                 mac_str, MY_DEVICE_CODE);

        printf("[HTTP] Body to send: %s\r\n", json_body); // In ra để debug

        // 6. Tạo HTTP Packet hoàn chỉnh (Header + Body)
        int len = sprintf(buf,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n" // Tự động điền độ dài body
            "Connection: close\r\n"
            "\r\n"
            "%s", // Body nằm ở đây
            HTTP_URL,
            HTTP_SERVER_IP,
            (int)strlen(json_body),
            json_body);


    if (write(sock, buf, len) < 0) {
        printf("[HTTP] Error: Send failed\r\n");
        goto exit;
    }

    // 7. Nhận phản hồi
    printf("[HTTP] Waiting for Response...\r\n");
    memset(buf, 0, 1024);
    int rlen = read(sock, buf, 1023);

    if (rlen > 0) {
        buf[rlen] = '\0'; // Null-terminate

        printf("\r\n--- [SERVER RESPONSE START] ---\r\n");
        printf("%s", buf);
        printf("\r\n--- [SERVER RESPONSE END] ---\r\n");

        if (strstr(buf, "200 OK") || strstr(buf, "201 Created")) {
            char *json_body = strstr(buf, "\r\n\r\n");

            if(json_body) {
                json_body += 4;
                printf("[HTTP] Parsing JSON Body: %s\r\n", json_body);

                cJSON *root = cJSON_Parse(json_body);
                if (root) {
                    cJSON *broker = cJSON_GetObjectItem(root, "broker");
                    cJSON *user   = cJSON_GetObjectItem(root, "user");
                    cJSON *pass   = cJSON_GetObjectItem(root, "pass");
                    cJSON *token  = cJSON_GetObjectItem(root, "token");

                    // [FIX] Kiểm tra thủ công: Không NULL và đúng kiểu String
                    if ((broker && broker->type == cJSON_String) &&
                        (token  && token->type == cJSON_String)) {

                        printf("[HTTP] Registration Success!\r\n");
                        printf("       Broker: %s\r\n", broker->valuestring);

                        // Lưu Flash (Dùng toán tử 3 ngôi để tránh crash nếu user/pass null)
                        storage_save_mqtt_info(broker->valuestring,
                                               (user && user->type == cJSON_String) ? user->valuestring : "",
                                               (pass && pass->type == cJSON_String) ? pass->valuestring : "",
                                               token->valuestring);
                        ret = 1;
                    } else {
                        printf("[HTTP] Error: JSON missing 'broker' or 'token'\r\n");
                    }
                    cJSON_Delete(root);
                } else {
                    printf("[HTTP] Error: JSON Parse Failed\r\n");
                }
            }
        } else {
            printf("[HTTP] Error: Not 200 OK\r\n");
        }
    } else {
        printf("[HTTP] Error: Empty/Timeout\r\n");
    }

exit:
    if (sock >= 0) close(sock);
    vPortFree(buf);
    return (ret == 1);
}
