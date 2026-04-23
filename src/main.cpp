#include <Arduino.h>
#include <driver/twai.h>

void setup() {
 Serial.begin(115200);
 twai_general_config_t genConfig = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5,GPIO_NUM_4,TWAI_MODE_NORMAL); //5 is tx and 4 is rx
 twai_timing_config_t timingConfig = TWAI_TIMING_CONFIG_500KBITS(); //Change to actual CAN baud rate
 twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

 if(twai_driver_install(&genConfig,&timingConfig,&filterConfig) == ESP_OK){
  Serial.println("TWAI driver installed");
 }
 if(twai_start() == ESP_OK){
  Serial.println("TWAI Driver started");
 }
 
}

void loop() {
  twai_message_t rxMessage;
  twai_message_t txMessage;
  //dummy messages change to actual messages that you want to send
  txMessage.identifier = 0x256;
  txMessage.data_length_code = 1;
  txMessage.data[0] = 0x111;
  txMessage.flags = TWAI_MSG_FLAG_EXTD;
  esp_err_t rxResult = twai_receive(&rxMessage, pdMS_TO_TICKS(100));
  esp_err_t txResult = twai_transmit(&txMessage,pdMS_TO_TICKS(100));

  if(rxResult == ESP_OK) {
      Serial.println("Message recieved successfully");
    } 
    else if (rxResult == ESP_ERR_TIMEOUT) {
      Serial.println("No message received.");
    } 
    else {
      Serial.println("Receive error!");
    }

  if(txResult == ESP_OK){
    Serial.println("Message transmitted successfully");
  }
  else if(txResult == ESP_ERR_TIMEOUT){
    Serial.println("No message transmitted");
  }
  else{
    Serial.println("Transmit error");
  }
}
