#include "Ota.h"

void initializeOTAService(){
  //Funções OTA
    ArduinoOTA.setHostname("PSRSwitchUpdate"); // Define o hostname (opcional)
    ArduinoOTA.setPassword("password"); // Define a senha (opcional)
  
    //define o que será executado quando o ArduinoOTA iniciar
    ArduinoOTA.onStart([](){ startOTA(); }); //startOTA é uma função criada para simplificar o código 

    //define o que será executado quando o ArduinoOTA terminar
    ArduinoOTA.onEnd([](){ Serial.println("\nEnd");; });

    //define o que será executado quando o ArduinoOTA estiver gravando
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); }); //progressOTA é uma função criada para simplificar o código 

    //define o que será executado quando o ArduinoOTA encontrar um erro
    ArduinoOTA.onError([](ota_error_t error){ errorOTA(error); });//errorOTA é uma função criada para simplificar o código 
    
    //inicializa ArduinoOTA
    ArduinoOTA.begin();
}

void stopOTAService(){
  ArduinoOTA.end();
}

//funções de exibição dos estágios de upload (start, progress, end e error) do ArduinoOTA
void startOTA(){
   String type;
   
   //caso a atualização esteja sendo gravada na memória flash externa, então informa "flash"
    if (ArduinoOTA.getCommand() == U_FLASH)
        type = "flash";
    else  //caso a atualização seja feita pela memória interna (file system), então informa "filesystem"
        type = "filesystem"; // U_SPIFFS

    //exibe mensagem junto ao tipo de gravação
    Serial.println("Start updating " + type);
}

//caso aconteça algum erro, exibe especificamente o tipo do erro
void errorOTA(ota_error_t error){  
      Serial.printf("Error[%u]: ", error);
      
      if (error == OTA_AUTH_ERROR) 
        Serial.println("Auth Failed");
      else
      if (error == OTA_BEGIN_ERROR)
        Serial.println("Begin Failed");
      else 
      if (error == OTA_CONNECT_ERROR)
        Serial.println("Connect Failed");
      else
      if (error == OTA_RECEIVE_ERROR) 
        Serial.println("Receive Failed");
      else 
      if (error == OTA_END_ERROR)
        Serial.println("End Failed");
}
//=============================================================END-FUNÇÕES-OTA==================================================================================================
