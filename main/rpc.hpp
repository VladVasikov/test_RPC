
#include "esp_log.h"
#include "driver/uart.h"
#include <vector>
#include <string>
#include <map>
#include <utility>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "crc.hpp"
#include <mutex>

#define RX_BUF_SIZE 100
#define TX_BUF_SIZE 100
#define MAX_RX_OUT_BUF 5
class rpc{
    public:
        rpc(): countMessage(0){
            uart_config = {
           .baud_rate = 115200,
           .data_bits = UART_DATA_8_BITS,
           .parity = UART_PARITY_DISABLE,
           .stop_bits = UART_STOP_BITS_1,
           .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
           .source_clk = UART_SCLK_DEFAULT,
         };    
        }

        void init(){
            ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
            ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
            uart_driver_install(UART_NUM_1, 1024, 1024, 0, NULL, 0);
            rxQueue = xQueueCreate(1, sizeof(std::vector<uint8_t>) );    
            txQueue = xQueueCreate(1, sizeof(std::vector<uint8_t>) ); 
            rxMessageQueue = xQueueCreate(1, sizeof(std::vector<uint8_t>) ); 
            txMessageQueue = xQueueCreate(1, sizeof(std::vector<uint8_t>) );     
            rxOutMessageQueue = xQueueCreate(MAX_RX_OUT_BUF, sizeof(std::vector<uint8_t>) );        
            startrxMassegTask();
            startProcessRxDataTask();
            startRxUartTask();
            startTxUartTask();
            startProcessTxData();
            startGlobalProcess();
        }
        
        QueueHandle_t* getRxOutQueue(){
            return &rxOutMessageQueue;
        }

        void sendCommand(std::string cmd, std::vector<uint8_t> arg){                                   
            dataSend.resize(3+arg.size()+cmd.size());
            dataSend[0]=0x0B;
            dataSend[1] = countMessage;                        
            std::copy(cmd.begin(), cmd.end(), dataSend.begin()+2);                        
            std::copy(arg.begin(), arg.end(), dataSend.begin()+3+ cmd.size());
            esp_log_buffer_hex("sendCommand: ", (void*)&(*dataSend.begin()), dataSend.size());
            xQueueSendToBack(txMessageQueue, &dataSend, portMAX_DELAY);            
            std::lock_guard guard(_mutex);
            listTxMassege[countMessage] = xTaskGetTickCount();
            countMessage++;          
        }

    private:
         std::vector<uint8_t> dataSend;
         uart_config_t uart_config; 
         uint8_t countMessage;
         const char *RPC_TAG = "RPC";
         std::map<uint8_t, uint32_t> listTxMassege;
         mutable std::mutex _mutex;
         const uint8_t lifeTimePack=100;
         TaskHandle_t _readSerialTaskHandle = nullptr; 
         TaskHandle_t _writeSerialTaskHandle = nullptr;     
         QueueHandle_t rxQueue = nullptr;
         QueueHandle_t rxMessageQueue = nullptr;
         QueueHandle_t txQueue = nullptr;
         QueueHandle_t txMessageQueue = nullptr;
         QueueHandle_t rxOutMessageQueue = nullptr;

        void responseRx(std::vector<uint8_t> data){            
            std::lock_guard guard(_mutex);
            listTxMassege.erase(data[1]);
        }

        void responseErrorRx(std::vector<uint8_t> data){
            std::lock_guard guard(_mutex);
            listTxMassege.erase(data[1]);
        }

        static void rxMassegTaskEntry(void* parameter ){
            static_cast<rpc*>(parameter)->rxMassegTask();            
        }

        static void processRxDataTaskEntry(void* parameter ){
            static_cast<rpc*>(parameter)->processRxDataTask();            
        }

        static void RxUartTaskEntry(void* parameter ){
            static_cast<rpc*>(parameter)->RxUartTask();            
        }

        static void processTxDataTaskEntry(void* parameter ){
            static_cast<rpc*>(parameter)->processTxData();            
        }

        static void TxUartTaskEntry(void* parameter ){
            static_cast<rpc*>(parameter)->TxUartTask();            
        }

        static void globalProcessEntry(void* parameter ){
            static_cast<rpc*>(parameter)->globalProcess();            
        }        

        void startGlobalProcess(){
            const BaseType_t taskCreated = xTaskCreate(
                globalProcessEntry,
                "globalProcessEntry",
                1024,
                this,
                tskIDLE_PRIORITY+1,
                NULL);        
            configASSERT(taskCreated == pdPASS);
        }
        
        void startProcessTxData(){
            const BaseType_t taskCreated = xTaskCreate(
                processTxDataTaskEntry,
                "processTxData",
                4096,
                this,
                tskIDLE_PRIORITY+1,
                NULL);        
            configASSERT(taskCreated == pdPASS);
        }

        void startTxUartTask(){
            const BaseType_t taskCreated = xTaskCreate(
                TxUartTaskEntry,
                "TxUart",
                4096,
                this,
                tskIDLE_PRIORITY+1,
                NULL);
        
            configASSERT(taskCreated == pdPASS);
        }

        void startRxUartTask(){
            const BaseType_t taskCreated = xTaskCreate(
                RxUartTaskEntry,
                "RxUart",
                4096,
                this,
                tskIDLE_PRIORITY+1,
                NULL);
        
            configASSERT(taskCreated == pdPASS);
        }

        void startrxMassegTask(){
            const BaseType_t taskCreated = xTaskCreate(
                rxMassegTaskEntry,
                "rxMasseg",
                4096,
                this,
                tskIDLE_PRIORITY+1,
                NULL);        
            configASSERT(taskCreated == pdPASS);
        }

        void startProcessRxDataTask(){
            const BaseType_t taskCreated = xTaskCreate(
                processRxDataTaskEntry,
                "processRxData",
                4096,
                this,
                tskIDLE_PRIORITY+1,
                NULL);        
            configASSERT(taskCreated == pdPASS);
        }

        void globalProcess(){
           while(1){
               ESP_LOGI(RPC_TAG, "globalProcess ");
              processListTxMassege(); 
              vTaskDelay(100/portTICK_PERIOD_MS);              
           }

        }
        
        void processListTxMassege(){
            std::lock_guard guard(_mutex);
            for (auto it = listTxMassege.begin(); it != listTxMassege.end(); it++) {        
                if( (xTaskGetTickCount() - it->second)*portTICK_PERIOD_MS > lifeTimePack ){
                    ESP_LOGI(RPC_TAG, "TimeOut cmd№  %d", it->second);
                    listTxMassege.erase(it);  //ответ на команду не пришел                    
                }
            }
        }

        void rxMassegTask(){
            std::vector<uint8_t> data;
            while(1){
                xQueueReceive(rxMessageQueue, &data, portMAX_DELAY); 
                switch(data[0]){
                    case 0x0B://запрос
                        xQueueSend(rxOutMessageQueue, &data, 0);   
                    break;
                    case 0x0C://стрим
                    break;
                    case 0x16://ответ
                      responseRx(data);
                    break;
                    case 0x21://ошибка
                      responseErrorRx(data);
                    break;
                }
            }
        }

        void TxUartTask(){
            std::vector<uint8_t> data;                                    
            while (1) {                
                    xQueueReceive( txQueue, &data, portMAX_DELAY );
                    const int txBytes = uart_write_bytes(UART_NUM_1, &(*data.begin()), data.size());                    
                    esp_log_buffer_hex("Write UART data: ", (void*)&(*data.begin()), data.size());
                   // ESP_LOGI(TX_TASK_TAG, txBytes);               
            }
        }

        void processTxData(){
            std::vector<uint8_t> data; 
            std::vector<uint8_t> dataIn;      
            while(1){
                xQueueReceive( txMessageQueue, &dataIn, portMAX_DELAY );                
                uint16_t _size = dataIn.size()+7;
                data.resize(_size);
                data[0] = 0xFA;
                data[1] = _size;
                data[2] = _size>>8;
                data[3] = crc8(&(*data.begin()), 3);
                data[4] = 0xFB;                
                std::copy(dataIn.begin(), dataIn.end(), data.begin()+5);
                data[_size-2] = crc8( &(*data.begin()), data.size()-2);
                data[_size-1] = 0xFE;                                                   
                xQueueSendToBack(txQueue, &data, portMAX_DELAY);                
            }
        }

        void RxUartTask(){            
            esp_log_level_set(RPC_TAG, ESP_LOG_INFO);
            std::vector<uint8_t> data;
            data.resize(RX_BUF_SIZE);  
            while (1) {
               const int rxBytes = uart_read_bytes(UART_NUM_1, &(*data.begin()), data.size(), 100 / portTICK_PERIOD_MS);
                if (rxBytes > 0) {
                    std::vector<uint8_t> buf(rxBytes);
                    std::copy(data.begin(), data.begin()+rxBytes, buf.begin());
                    xQueueSendToBack(rxQueue, &buf, portMAX_DELAY);   
                }   
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }  

        void processRxDataTask(){
            std::vector<uint8_t> data;
            std::vector<uint8_t> dataOut;
            uint16_t size_pac;
            while(1){
                xQueueReceive( rxQueue, &data, portMAX_DELAY ); 
                for(auto i =0; i< data.size(); i++){
                    if(data[0] == 0xFA){break;}
                    else{data.erase(data.begin());}
                }                               
                if (data.size() > 6){
                    size_pac = data[1];
                    size_pac |= data[2] << 8;  
                    uint8_t crc_val =  crc8(&(*data.begin()), 3);                                   
                    if(crc8(&(*data.begin()), 3) == data[3] ){
                        if(data.size() > size_pac ){
                            data.erase(data.begin()+size_pac, data.end());
                        } 
                        if(data.size() == size_pac ){                            
                            if( crc8(&(*data.begin()), data.size()-2) == *(data.end()-2) ){
                                if((data[4]==0xFB) && (*(data.end()-1) ==0xFE) ){
                                    dataOut.resize(data.size()-7);
                                    std::copy(data.begin()+5, data.end()-2, dataOut.begin());
                                    xQueueSendToBack(rxMessageQueue, &dataOut, portMAX_DELAY); 
                                    esp_log_buffer_hex("Read UART data: ", (void*)&(*dataOut.begin()), dataOut.size());
                                }else{ESP_LOGI(RPC_TAG, "Error start or stop byte");}
                            }else{ ESP_LOGI(RPC_TAG, "Error all crc"); }
                        }     
                    } else{ ESP_LOGI(RPC_TAG, "Error title crc"); }
                }else{ ESP_LOGI(RPC_TAG, "Error size packet"); }
            }
        }
};
