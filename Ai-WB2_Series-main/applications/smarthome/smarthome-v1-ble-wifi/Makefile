#
# PROJECT MAKEFILE
#

PROJECT_NAME := smarthome-v1-ble-wifi
PROJECT_PATH := $(abspath .)
PROJECT_BOARD := evb
export PROJECT_PATH PROJECT_BOARD

# --- CẤU HÌNH BLE ---
BLECONTROLLER_LIBS := std m0s1 m0s1s
BLECONTROLLER_LIB_DEFAULT := std
BLECONTROLLER_LIB_PREFIX := blecontroller_602_

-include ./proj_config.mk

ifeq ($(origin BL60X_SDK_PATH), undefined)
BL60X_SDK_PATH_GUESS ?= $(shell pwd)
BL60X_SDK_PATH ?= $(BL60X_SDK_PATH_GUESS)/../../..
$(info ****** Please SET BL60X_SDK_PATH ******)
$(info ****** Trying SDK PATH [$(BL60X_SDK_PATH)])
endif

# --- ĐỊNH NGHĨA CÁC MODULE ---
COMPONENTS_NETWORK := sntp dns_server
COMPONENTS_BLSYS   := bltime blfdt blmtd blota bloop loopadc looprt loopset
COMPONENTS_VFS     := romfs

# [QUAN TRỌNG] Thêm bộ thư viện MQTT của Ai-Thinker (Lấy từ demo của bạn)
COMPONENTS_MQTT    := axk_common tcp_transport http-parser axk_tls axk_mqtt

# --- CẤU HÌNH BLE LIBRARY ---
ifeq ($(CONFIG_BLECONTROLLER_LIB),all)
COMPONENTS_BLECONTROLLER := $(addprefix $(BLECONTROLLER_LIB_PREFIX), $(BLECONTROLLER_LIBS))
else
ifeq ($(findstring $(CONFIG_BLECONTROLLER_LIB), $(BLECONTROLLER_LIBS)),)
COMPONENTS_BLECONTROLLER := $(addprefix $(BLECONTROLLER_LIB_PREFIX), $(BLECONTROLLER_LIB_DEFAULT))
else
COMPONENTS_BLECONTROLLER := $(addprefix $(BLECONTROLLER_LIB_PREFIX), $(CONFIG_BLECONTROLLER_LIB))
endif
endif

ifeq ($(CONFIG_BT_TL),1)
COMPONENTS_BLE     := $(COMPONENTS_BLECONTROLLER)
else
COMPONENTS_BLE     := $(COMPONENTS_BLECONTROLLER) blestack blecontroller
endif

# --- INCLUDE COMPONENTS ---
# Kết hợp Wifi, System, BLE và MQTT vào một danh sách
INCLUDE_COMPONENTS += freertos_riscv_ram bl602 ble602_hal bl602_std newlibc \
                      wifi wifi_manager wpa_supplicant bl602_os_adapter wifi_hosal hosal \
                      mbedtls_lts lwip lwip_dhcpd vfs yloop utils cli netutils blog blog_testc blcrypto_suite \
                      easyflash4 coredump bl_os_adapter cjson

INCLUDE_COMPONENTS += $(COMPONENTS_NETWORK)
INCLUDE_COMPONENTS += $(COMPONENTS_BLSYS)
INCLUDE_COMPONENTS += $(COMPONENTS_VFS)
INCLUDE_COMPONENTS += $(COMPONENTS_MQTT)   # <--- Đã thêm MQTT vào đây
INCLUDE_COMPONENTS += $(PROJECT_NAME)

ifeq ($(CONFIG_BT),1)
INCLUDE_COMPONENTS += $(COMPONENTS_BLE)
ifeq ($(CONFIG_BT_MESH),1)
INCLUDE_COMPONENTS += blemesh
endif
endif

include $(BL60X_SDK_PATH)/make_scripts_riscv/project.mk