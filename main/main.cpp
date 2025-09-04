#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include <vector>
#include "rpc.hpp"

extern "C" void app_main(void)
{
  {
    rpc _rpc;  
    _rpc.init();
    uint8_t cnt=1;   
    std::vector<uint8_t> data = {12,22};
    for( ;; ){        
          
        //data[0] = 1;
        _rpc.sendCommand(std::string("Test"), data);
        cnt++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
}