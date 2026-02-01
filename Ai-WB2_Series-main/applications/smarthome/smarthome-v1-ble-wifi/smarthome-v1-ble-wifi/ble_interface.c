#include "ble_interface.h"
#include <FreeRTOS.h>
#include <task.h>

// Include các thư viện gốc của SDK
#include "hci_driver.h"
#include "ble_lib_api.h"
#include "bluetooth.h"

void ble_stack_start(ble_init_complete_cb_t cb)
{
    // 1. Init Controller (Lớp vật lý)
    ble_controller_init(configMAX_PRIORITIES - 1);
    
    // 2. Init Host Interface
    hci_driver_init();

    // 3. Enable Stack và gọi callback khi xong
    bt_enable(cb);
}

void ble_stack_stop(void)
{
    // Hàm disable của BL602 đôi khi không ổn định,
    // nhưng về mặt driver thì ta gọi hàm này.
    bt_disable();
}
