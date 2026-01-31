#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <easyflash.h>
#include <bl_sys.h>
#include <FreeRTOS.h>
#include <task.h>
#include "app_storage.h"

void storage_init(void) {
    easyflash_init();
}

void trim_str(char *str) {
    char *p = str;
    int l = strlen(p);
    while(l > 0 && (isspace((unsigned char)p[l - 1]) || p[l - 1] == '\r' || p[l - 1] == '\n')) p[--l] = 0;
    while(*p && (isspace((unsigned char)*p) || *p == '\r' || *p == '\n')) p++;
    if(p != str) memmove(str, p, strlen(p) + 1);
}

void save_wifi_and_reboot(char* ssid, char* pass) {
    trim_str(ssid);
    trim_str(pass);
    printf(">>> [STORAGE] SAVE & REBOOT: [%s] [%s]\r\n", ssid, pass);
    ef_set_env("my_ssid", ssid);
    ef_set_env("my_pass", pass);
    ef_save_env();
    vTaskDelay(500);
    bl_sys_reset_por();
}

int get_wifi_safe(char *ssid_out, char *pass_out) {
    memset(ssid_out, 0, 33);
    memset(pass_out, 0, 64);
    char *temp_ssid = ef_get_env("my_ssid");
    if (temp_ssid) strcpy(ssid_out, temp_ssid);
    char *temp_pass = ef_get_env("my_pass");
    if (temp_pass) strcpy(pass_out, temp_pass);
    return (strlen(ssid_out) > 0);
}
