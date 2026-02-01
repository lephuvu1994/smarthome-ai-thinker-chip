#ifndef APP_HTTP_H
#define APP_HTTP_H

// Hàm này sẽ thực hiện toàn bộ quy trình:
// 1. Lấy MAC -> 2. POST lên API -> 3. Nhận JSON -> 4. Parse -> 5. Lưu vào Storage
// Trả về: 1 nếu thành công, 0 nếu thất bại
int app_http_register_device(void);

#endif
