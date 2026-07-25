#include <Arduino.h>
#include <driver/twai.h>
#include <string>

//change to whatever GPIO pin you want
#define txPin GPIO_NUM_5
#define rxPin GPIO_NUM_4

int mode;
twai_message_t message;

void setup() {
 Serial.begin(115200);
 twai_general_config_t genConfig = TWAI_GENERAL_CONFIG_DEFAULT(txPin,rxPin,TWAI_MODE_NORMAL);
 twai_timing_config_t timingConfig = TWAI_TIMING_CONFIG_500KBITS(); //Change to actual CAN baud rate
 twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

 if(twai_driver_install(&genConfig,&timingConfig,&filterConfig) == ESP_OK){
  Serial.println("TWAI driver installed");
 }
 if(twai_start() == ESP_OK){
  Serial.println("TWAI Driver started");
 }
  Serial.println("Enter what mode you want to use\n1 for recieve\n2 for transmit");
  bool validInput = false;
    if(Serial.available() > 0){
      while(!validInput){
        String tempString = Serial.readStringUntil('\n');
        tempString.trim();
        mode = tempString.toInt();
        switch(mode){
          case 1:
            Serial.println("Entering recieve mode");
            validInput = true;
            break;
          
          case 2: 
            Serial.println("Entering transmit mode");
            configMSG(&message);  
            validInput = true;
            break;
          
          default:
            Serial.println("Enter either 1 or 2");
        }
      }
    }
}

void loop() {
  esp_err_t result;
  switch(mode){
    case 1:
      result = twai_receive(&message,pdMS_TO_TICKS(500));
      break;
    
    case 2:
      result = twai_transmit(&message,pdMS_TO_TICKS(500));
      break;

    default:
      return;
  }

  errorHandler(result);
  switch(result){
    case ESP_OK:
      printMSG(&message);
      break;

    default:
      break;
  }
}

void configMSG(twai_message_t* message){
  message->flags = TWAI_MSG_FLAG_NONE;
  Serial.println("Enter CAN ID hex value\n");
  char id[8];

  Serial.readBytesUntil('\n',id, sizeof(char) * 2); 
  message->identifier = std::stol(id,nullptr,16);
  Serial.println("Enter data length\n");
  message->data_length_code = Serial.parseInt();
  return;  
}

void printMSG(twai_message_t* message){
  Serial.printf("ID %u\n",message->identifier);
  for(int i =0; i < message->data_length_code; i++){
    Serial.printf("Byte %d data %d\n",i, message->data[i]);
  }
}

void errorHandler(esp_err_t result){
    switch(result){
    case ESP_OK:
      Serial.println("Success");
      break;

    case ESP_ERR_TIMEOUT:
      Serial.println("Message timeout");
      break;

    default:
      Serial.println("CAN error");
  }
}