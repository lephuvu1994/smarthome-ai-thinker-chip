#include "app_conf.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cJSON.h>
#include <string.h>
#include <stdio.h>

// Hàm helper gửi POST/GET đơn giản
int http_request(const char* host, const char* path, const char* json_body, char* out_resp) {
    // ... (Code setup socket TCP port 80/443 connection) ...
    // ... (Gửi String: "POST /path HTTP/1.1...") ...
    // ... (Nhận phản hồi vào out_resp) ...
    // Mình viết tắt đoạn socket boilerplate để tiết kiệm chỗ
    // Bạn có thể dùng ví dụ http_client trong SDK
    return 1; // Success
}

// 1. Lấy Config Server thật từ Domain mồi
int app_api_bootstrap() {
    char resp[1024] = {0};
    // GET /config
    if (http_request(BOOTSTRAP_DOMAIN, "/config", NULL, resp)) {
        cJSON *json = cJSON_Parse(resp); // Cần xử lý bỏ Header HTTP
        // Parse lấy "api" và "mqtt" lưu vào g_cfg
        // storage_save_bootstrap(...)
        cJSON_Delete(json);
        return 1;
    }
    return 0;
}

// 2. Kích hoạt thiết bị (Chống copy)
int app_api_activate(char* mac_str) {
    char req[256];
    char resp[1024] = {0};

    sprintf(req, "{\"mac\":\"%s\"}", mac_str);

    if (http_request(g_cfg.api_host, "/activate", req, resp)) {
        // Parse JSON
        // Lấy STT và TOKEN
        // storage_save_identity(stt, token);
        return 1; // Success
    }
    return 0; // Fail (Mạng hoặc Hết quota)
}
