#ifndef APP_STORAGE_H
#define APP_STORAGE_H

void storage_init(void);
void trim_str(char *str);
void save_wifi_and_reboot(char* ssid, char* pass);
int get_wifi_safe(char *ssid_out, char *pass_out);

#endif
