#include <Arduino.h>
#include <WiFiManager.h> 
#include <EEPROM.h>
#include "fauxmoESP.h"

void wifiSetup();
String readName(byte);
void changeDeviceName(String, byte);
void resetDevice();
void rebootDevice();
void touchCalibration();
void handleTouch();
void checkWiFiConnection();

//================EEPROM==================//
//  SIZE: 512                             //
//      000-250 10 Names 25 bytes each    //
//      161-188 ApiKey                    //
//      500     Device StartStatus        //
//                                        //
//                                        //
//========================================//        


//============PARAMETRIZATION==========//
#define SERIAL_BAUDRATE       115200
#define DEVICE_TYPE           "VENTILADOR"
#define OUTPUT_PIN_1          12
#define OUTPUT_PIN_2          25
#define OUTPUT_PIN_3          32

//26, 25, 33 -> Ventilador Grande
//12, 25, 32 -> Ventilador Pequeno


#if defined(ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3)
  #define TOUCH_ENABLED       true
  #define MULTI_CORE          true
  void TaskHandleWiFi(void *pvParameters); //Para o ESP32, o wifi serão tratados em um núcleo a parte
#else
  #define TOUCH_ENABLED       false
  #define MULTI_CORE          false
#endif

#if TOUCH_ENABLED 
    #define INPUT_PIN       4
    #define INPUT_PIN2      14
    #define INPUT_PIN3      15
    #define INPUT_PIN4      27
    bool pressed = false;
    byte touch_sensibility;
    unsigned long touchResetStartTime = 0; // Armazena o tempo em que o toque começou
    const unsigned long resetThreshold = 10000; 

    //4, 2, 15, 0 -> Ventilador Grande
    //4, 14, 15, 27 -> Ventilador Pequeno

#endif

bool connected = true;
byte deviceNameLength = 25;
unsigned long lastConnectAttempt = 0;
fauxmoESP fauxmo;
WiFiManager wifiManager;


void setup() {   

    Serial.begin(SERIAL_BAUDRATE);
    Serial.println("\nIniciando...");

    pinMode(OUTPUT_PIN_1, OUTPUT);       
    pinMode(OUTPUT_PIN_2, OUTPUT);  
    pinMode(OUTPUT_PIN_3, OUTPUT);      
    
    wifiSetup();

    

    //Lê o nome da eeprom    //String name = readName(0);        //char deviceName[name.length() + 1];    //strcpy(deviceName, name.c_str());
    // Add virtual devices -> &readName(0)[0] = pointer to a char fom a string
    fauxmo.addDevice(&readName(0)[0], DEVICE_TYPE, OUTPUT_PIN_1);
   
    fauxmo.onSetState([](unsigned char device_id, char * device_name, bool state, unsigned char value, byte pin) {
        Serial.printf("[MAIN] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);    
        if(state == false)        
        {
            digitalWrite(OUTPUT_PIN_1, LOW);
            digitalWrite(OUTPUT_PIN_2, LOW);
            digitalWrite(OUTPUT_PIN_3, LOW);
        }        
        
        else if(value <= 85)  {
            digitalWrite(OUTPUT_PIN_1, HIGH);
            digitalWrite(OUTPUT_PIN_2, LOW);
            digitalWrite(OUTPUT_PIN_3, LOW);
        }   
        else if (value > 85 && value <= 170)  {
            digitalWrite(OUTPUT_PIN_1, LOW);
            digitalWrite(OUTPUT_PIN_2, HIGH);
            digitalWrite(OUTPUT_PIN_3, LOW);
        } 
        else if (value > 170)  {
            digitalWrite(OUTPUT_PIN_1, LOW);
            digitalWrite(OUTPUT_PIN_2, LOW);
            digitalWrite(OUTPUT_PIN_3, HIGH);
        } 
         
    });

    bool startState = getStartState();

    //para ventilador inicia com potencia minima
    if(strcmp(DEVICE_TYPE, "VENTILADOR") == 0 ) 
        fauxmo.setState(0,startState,1);
    else 
        fauxmo.setState(0,startState,startState ? 1 : 0);

    fauxmo.startState = startState;
    fauxmo.nameLenth = &deviceNameLength;

    // By default, fauxmoESP creates it's own webserver on the defined port, The TCP port must be 80 for gen3 devices (default is 1901), has to be done before the call to enable()
    fauxmo.setPort(80); // This is required for gen3 devices
    fauxmo.enable(true); // You have to call enable(true) once you have a WiFi connection

    //Define o comando para resetar o dispositivo.
    fauxmo.onReset([]{resetDevice();});
    //fauxmo.onSetOtaState([]{startOta();});
    touchCalibration();
    //initializeOTAService();

    //Configura o segundo núcleo do ESP32 para verificar o wifi
    #if MULTI_CORE
      xTaskCreatePinnedToCore(
          TaskHandleWiFi,       // Função da tarefa
          "WiFiTask",           // Nome da tarefa (para debug)
          4096,                 // Tamanho da pilha (em bytes)
          NULL,                 // Parâmetro passado para a tarefa (opcional)
          1,                    // Prioridade da tarefa
          NULL,                 // Handle da tarefa (opcional)
          0                     // Núcleo onde a tarefa será executada (0 ou 1)
      );
    #endif
    
}

void loop() {
    fauxmo.handle();
    handleTouch();

    #if !MULTI_CORE
      checkWifiConnection(); 
    #endif
}


//======================================WIFI============================================================================
void checkWifiConnection(){

  if (WiFi.status() != WL_CONNECTED && WiFi.status() != WL_CONNECTION_LOST){
        connected = false;
        
        //Alimenta o portal, se estiver ativo
        if (wifiManager.getConfigPortalActive()) 
            wifiManager.process(); 

        //Caso não tenha encontrado a rede tenta se conectar manualmente a cada 5 segundos, útil em quedas de energia quando o dispositivo liga antes do roteador
        //Apenas quando iniciar e não encontrar, se cair enquanto o dispositivo estiver ligado o wifi manager reconecta sozinho
        if(WiFi.status() != WL_CONNECTION_LOST && WiFi.status() != WL_CONNECTED && millis() - lastConnectAttempt > 5000 && wifiManager.getWiFiSSID() != ""){
            lastConnectAttempt = millis();
            Serial.println("Tentando conectar...");
            WiFi.disconnect();
            WiFi.begin(wifiManager.getWiFiSSID().c_str(), wifiManager.getWiFiPass(true).c_str()); 


            if (WiFi.waitForConnectResult() == WL_CONNECTED) {
                Serial.println("Conexão bem-sucedida!");
                connected = true;
                fauxmo.enable(false);
                Serial.println("Fauxmo desatilitado");
                delay(3000);
                fauxmo.enable(true);
                Serial.println("Fauxmo habilitado");

            } else {
                Serial.println("Falha na conexão.");
            }
            
            //WiFi.disconnect();
            //Serial.println(wifiManager.getWiFiSSID().c_str());
            //Serial.println(wifiManager.getWiFiPass().c_str());
            //WiFi.begin(wifiManager.getWiFiSSID().c_str(), wifiManager.getWiFiPass(true).c_str()); 
        }      
    }
    /*
    //reinicia após a configuração do portal, para que o servidor seja iniciado
    else if (connected == false) {
        fauxmo.enable(false);
        delay(3000);
        fauxmo.enable(true);
        connected = true;
    }    
    */
}

void wifiSetup() {
    //Obtem o nome do dispositivo e substitui os espaços por '-', neccessário no wifi.hostname
    String name = readName(0);
    name.replace(' ', '-');
    WiFi.hostname(name.c_str());

    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setBreakAfterConfig(true);
    wifiManager.setSaveConfigCallback(rebootDevice);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    //Tenta se conectar na ultima rede configurada, caso não consiga, cria uma rede com o nome do dispositivo para configuração
    wifiManager.autoConnect(name.c_str());
    
}

//============================================END WIFI==================================================================


//============================================EEPROM===================================================================

String readName(byte startAdress){  
    EEPROM.begin(512);
    
    // Se o endereço base for maior que o espaço de endereçamento da EEPROM retornamos um nome padrão
    if (startAdress>EEPROM.length()){
        EEPROM.end();
        return "PSR Switch";
    } 
 
    String deviceName;
     // Caso contrário a string a partir do endereço informado
    #ifdef ESP32
        deviceName = EEPROM.readString(startAdress);
        //Se atingir o endereço 25 e não encontrar o final da string, retorna o nome padrão. Para dispositivos ainda sem nome ou com problema na string
        //if (deviceName == "" || deviceName.length() > deviceNameLength) return "PSR Switch";
         if (deviceName == "" ) return "PSR Switch";
        
    #else
        int address = startAdress;
        char pos;
        do{
            pos = EEPROM.read(address); // Leitura do byte com base na posição atual
            address++; // A cada leitura incrementamos a posição a ser lida
            deviceName += pos; // Montamos string de saída
            
            //Se atingir o endereço 25 e não encontrar o final da string, retorna o nome padrão. Para dispositivos ainda sem nome
            if(address > startAdress + deviceNameLength){
                return "PSR Switch";              
            }           
        }
        while (pos != '\0'); // Fazemos isso até encontrar o marcador de fim de string
        
    #endif

    EEPROM.end();
    
    return deviceName; // Retorno da mensagem
}

bool getStartState(){

    EEPROM.begin(512);//Inicia a EEPROM com tamanho de 512 Bytes.
    bool state = EEPROM.read(500);
    EEPROM.end(); 
    return state;
}

//============================================END EEPROM===============================================================


//============================================TOUCH===================================================================

#if TOUCH_ENABLED 
    void touchCalibration(){

        //Verifica o valor "flutuante" 100 vezes, realiza a média e divide por 2, para determinar a partir de qual valor o pindo sera considerado acionado
        int total = 0;
        for(byte i = 0; i<100; i++) total += touchRead(INPUT_PIN); 
        touch_sensibility = (total/100)/2;
        Serial.println("Valor padrão:" + String(touch_sensibility)); 
    }

    void handleTouch(){
      int touchstat = 0;  

      //Código para reset
      if (touchRead(INPUT_PIN) < touch_sensibility) {

          for(int i=0; i< 20; i++){
              touchstat = touchstat + touchRead(INPUT_PIN); //Cem leituras para confirmar que não é um falso positivo ou ruido
              delay(5);                    
          } 
                
        touchstat = touchstat / 20; // obtém a média    
        
        if (touchstat <= touch_sensibility) {
            if (touchResetStartTime == 0) {
                touchResetStartTime = millis(); // Inicia o contador de tempo
            }

            // Verifica se o pino está pressionado por mais de 5 segundos
            if (millis() - touchResetStartTime >= resetThreshold) {
                Serial.println("Reset triggered!");
                digitalWrite(OUTPUT_PIN_1, HIGH);
                delay(200);
                digitalWrite(OUTPUT_PIN_1, LOW);
                delay(200);
                digitalWrite(OUTPUT_PIN_1, HIGH);
                delay(200);
                digitalWrite(OUTPUT_PIN_1, LOW);
                resetDevice();
            }
        }
      }

      else touchResetStartTime = 0; // Reseta o contador de tempo se o pino não estiver pressionado
        
      if(touchRead(INPUT_PIN) < touch_sensibility){
            if(!pressed){
                              
                for(int i=0; i< 20; i++){
                    touchstat = touchstat + touchRead(INPUT_PIN); //Cem leituras para confirmar que não é um falso positivo ou ruido
                    delay(5);                    
                } 
                
                touchstat = touchstat / 20; // obtém a média    
                
                if (touchstat <= touch_sensibility) {
                    Serial.println("Pin 1: " + String(touchstat));                    
                    fauxmo.setState(0,false,0); 
                    pressed = true;
                }                          
            }  
        }
        else if(touchRead(INPUT_PIN2) < touch_sensibility){   
            if(!pressed){   
                           
                for(int i=0; i< 20; i++){
                    touchstat = touchstat + touchRead(INPUT_PIN2); //Cem leituras para confirmar que não é um falso positivo ou ruido
                    delay(5);  
                } 
                touchstat = touchstat / 20; // obtém a média
                
                if (touchstat <= touch_sensibility) {
                    Serial.println("Pin 2: " + String(touchstat)); 
                    fauxmo.setState(0,true,1);
                    pressed = true;
                }
            }
        }
        else if(touchRead(INPUT_PIN3) < touch_sensibility){  
            if(!pressed){     
              
                for(int i=0; i< 20; i++){
                    touchstat = touchstat + touchRead(INPUT_PIN3); //Cem leituras para confirmar que não é um falso positivo ou ruido
                    delay(5);                          
                } 
                touchstat = touchstat / 20; // obtém a média
                
                if (touchstat <= touch_sensibility) {
                    Serial.println("Pin 3: " + String(touchstat)); 
                    fauxmo.setState(0,true,170);
                    pressed = true;
                } 
            }
        }
        else if(touchRead(INPUT_PIN4) < touch_sensibility){ 
            if(!pressed){    

                for(int i=0; i< 20; i++){
                    touchstat = touchstat + touchRead(INPUT_PIN4); //Cem leituras para confirmar que não é um falso positivo ou ruido
                    delay(5);  
                } 
                touchstat = touchstat / 20; // obtém a média
                
                if (touchstat <= touch_sensibility) {
                    Serial.println("Pin 4: " + String(touchstat)); 
                    fauxmo.setState(0,true,255);
                    pressed = true;
                } 
            }
        }
    
        else pressed = false;
    }
#else
void touchCalibration(){}
void handleTouch(){}
#endif

//============================================END TOUCH===============================================================

void resetDevice(){
    wifiManager.resetSettings();
    rebootDevice();
}

void rebootDevice(){
    #ifdef ESP32  
        ESP.restart();
    #else          
        ESP.reset();
    #endif  
}

#if MULTI_CORE
  void TaskHandleWiFi(void *pvParameters){
    for (;;) {
      checkWifiConnection();
      // Pequeno atraso para evitar uso intensivo da CPU
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
#endif