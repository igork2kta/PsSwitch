#include <Arduino.h>
#ifdef ESP32
    #include <WiFi.h>
#else
    #include <ESP8266WiFi.h>
#endif
#include <WiFiManager.h> 
#include <EEPROM.h>
#include "fauxmoESP.h"
#include "Ota.h"

void wifiSetup();
String readName(byte);
void changeDeviceName(String, byte);
void resetDevice();
void rebootDevice();
void touchCalibration();
void handleTouch();

//===========EEPROM======================//
//  SIZE: 512                            //
//      0-160  10 Names 16 bytes each    //
//      500    Device StartStatus        //
//                                       //
//                                       //
//=======================================//        


//============PARAMETRIZATION==========//
#define SERIAL_BAUDRATE     115200
#define DEVICE_TYPE         "PLUG"
#define TOUCH_ENABLED       false
#define OUTPUT_PIN_1          0
//#define OUTPUT_PIN_2        25
//#define OUTPUT_PIN_3        33

//26, 25, 33

#if TOUCH_ENABLED 
    #define INPUT_PIN       4
    //#define INPUT_PIN2      13
    //#define INPUT_PIN3      15
    //#define INPUT_PIN4      32
    bool pressed = false;
    byte touch_sensibility;
#endif

bool connected = true;

char static_ip[16] = "10.0.0.137";
char static_gw[16] = "10.0.0.1";
char static_sn[16] = "255.255.255.0";


fauxmoESP fauxmo;
WiFiManager wifiManager;


void setup() {   

    //gpio_config_t io_conf;
    //io_conf.mode =  GPIO_MODE_OUTPUT;
    //io_conf.pin_bit_mask = (1 << OUTPUT_PIN_1) | (1 << OUTPUT_PIN_2) | (1ULL << OUTPUT_PIN_3); //Se o pino for 32 ou mais, precisa do ULL 
    //gpio_config(&io_conf);  
    pinMode(OUTPUT_PIN_1, OUTPUT);       
    
    //Obtem o nome do dispositivo e substitui os espaços por '-', neccessário no wifi.hostname
    String name = readName(0);
    for (int i = 0; i < name.length(); i++) 
        if (name.charAt(i) == ' ') 
            name.setCharAt(i, '-');  
    
    //Seta o nome que irá aparecer em dispositivos como o roteador
    WiFi.hostname(name.c_str());

    //Lê o nome da eeprom    //String name = readName(0);        //char deviceName[name.length() + 1];    //strcpy(deviceName, name.c_str());
    // Add virtual devices -> &readName(0)[0] = pointer to a char fom a string
    fauxmo.addDevice(&readName(0)[0], DEVICE_TYPE, OUTPUT_PIN_1);
    //fauxmo.addDevice(&readName(16)[0], OUTPUT_PIN_2);
    //fauxmo.addDevice(&readName(32)[0], OUTPUT_PIN);   

    fauxmo.onSetState([](unsigned char device_id, char * device_name, bool state, unsigned char value, byte pin) {
       
        // Callback when a command from Alexa is received. 
        // You can use device_id or device_name to choose the element to perform an action onto (relay, LED,...)
        // State is a boolean (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
        // Just remember not to delay too much here, this is a callback, exit as soon as possible.
        // If you have to do something more involved here set a flag and process it in your main loop.
        
        Serial.printf("[MAIN] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);    
        
        // Checking for device_id is simpler if you are certain about the order they are loaded and it does not change.
        // Otherwise comparing the device_name is safer.

        //VENTILADOR START
        /*
        if(state == false)        
        {
            GPIO.out_w1tc = (1 << OUTPUT_PIN_1) | (1 << OUTPUT_PIN_2);
            GPIO.out1_w1tc.val = (1 << (OUTPUT_PIN_3 - 32));
        }        
        
        else if(value <= 85)  {
            GPIO.out_w1tc = (1 << OUTPUT_PIN_2);
            GPIO.out1_w1tc.val = (1 << (OUTPUT_PIN_3 - 32));
            GPIO.out_w1ts = (1 << OUTPUT_PIN_1);
        }   
        else if (value > 85 && value <= 170)  {
            GPIO.out_w1tc = (1 << OUTPUT_PIN_1);
            GPIO.out1_w1tc.val = (1 << (OUTPUT_PIN_3 - 32));
            GPIO.out_w1ts = (1 << OUTPUT_PIN_2);
        } 
        else if (value > 170)  {
            GPIO.out_w1tc = (1 << OUTPUT_PIN_1) | (1 << OUTPUT_PIN_2);
            GPIO.out1_w1ts.val = (1 << (OUTPUT_PIN_3 - 32));
        } 
        */
        //VENTILADOR END

        //OUTROS
        digitalWrite(pin, !state ?  HIGH : LOW);
         
    });

    bool startState = getStartState();
    
    //para ventilador inicia com potencia maxima
    if(strcmp(DEVICE_TYPE, "VENTILADOR") == 0 ) 
        fauxmo.setState(0,startState,255);
    else 
        fauxmo.setState(0,startState,startState ? 255 : 0);

    fauxmo.startState = startState;

    wifiSetup();   

    // By default, fauxmoESP creates it's own webserver on the defined port, The TCP port must be 80 for gen3 devices (default is 1901), has to be done before the call to enable()
    fauxmo.setPort(80); // This is required for gen3 devices
    fauxmo.enable(true); // You have to call enable(true) once you have a WiFi connection

    //Define o comando para resetar o dispositivo.
    fauxmo.onReset([]{resetDevice();});
    touchCalibration();
    initializeOTAService();

    Serial.begin(SERIAL_BAUDRATE);
    Serial.println("\n");
}

void loop() {

    // fauxmoESP uses an async TCP server but a sync UDP server
    // Therefore, we have to manually poll for UDP packets
    fauxmo.handle();
    ArduinoOTA.handle();
    handleTouch();

    //Caso não encontre rede, continua executando o codigo para funcionamento local 
    if (WiFi.status() != WL_CONNECTED){
        connected = false;
        
        //Alimenta o portal 
        if (wifiManager.getConfigPortalActive()) 
            wifiManager.process(); 
        
        //Caso não tenha encontrado a rede tenta se conectar manualmente, útil em quedas de energia
        //Apenas quando iniciar e não encontrar, se cair enquanto o dispositivo estiver ligado, o wifi manager reconecta sozinho
        if(WiFi.status() != WL_CONNECTION_LOST && WiFi.status() != WL_CONNECTED){
            WiFi.disconnect();
            WiFi.begin(wifiManager.getWiFiSSID().c_str(), WiFi.psk().c_str());
            delay(200);            
        }      
    }
    //reinicia após a configuração do portal, para que o servidor seja iniciado
    else if (connected == false) {
        fauxmo.enable(false);
        delay(3000);
        fauxmo.enable(true);
        connected = true;
    }       
        
    // This is a cheap way to detect memory leaks
    /*
    static unsigned long last = millis();
    if (millis() - last > 30000) {
        last = millis();
        Serial.printf("[MAIN] Free heap: %d bytes\n", ESP.getFreeHeap());
    }
    */

    
}

void wifiSetup() {
    
    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setBreakAfterConfig(true);
    //wifiManager.setConfigPortalTimeout(300);
    
    //wifiManager.setConfigPortalTimeoutCallback([](){rebootDevice();});
            
    //Serial.println(WiFi.macAddress());
    WiFi.mode(WIFI_STA);
    wifiManager.autoConnect(&readName(0)[0]);

    
    IPAddress _ip, _gw, _sn;
    _ip.fromString(static_ip);
    _gw.fromString(static_gw);
    _sn.fromString(static_sn);

    wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn);
    
}

String readName(byte startAdress){
  
  
    EEPROM.begin(512);//Inicia a EEPROM com tamanho de 512 Bytes.
    
    // Se o endereço base for maior que o espaço de endereçamento da EEPROM retornamos um nome padrão
    if (startAdress>EEPROM.length()){
        EEPROM.end();
        return "PSR Switch";
    } 
 
    String deviceName;
     // Caso contrário a string a partir do endereço informado
    #ifdef ESP32
        deviceName = EEPROM.readString(startAdress);
        if (deviceName == "") return "PSR Switch";
        
    #else
        int address = startAdress;
        char pos;
        do{
            pos = EEPROM.read(address); // Leitura do byte com base na posição atual
            address++; // A cada leitura incrementamos a posição a ser lida
            deviceName += pos; // Montamos string de saída
            
            //Se atingir o endereço 16 e não encontrar o final da string, retorna o nome padrão. Para dispositivos ainda sem nome
            if(address > startAdress + 16){
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


#if TOUCH_ENABLED 
    void touchCalibration(){

        //Verifica o valor "flutuante" 100 vezes, realiza a média e divide por 2, para determinar a partir de qual valor o pindo sera considerado acionado
        int total = 0;
        for(byte i = 0; i<100; i++) total += touchRead(INPUT_PIN); 
        touch_sensibility = (total/100)/2;
        Serial.println("Valor padrão:" + String(touch_sensibility)); 
    }

    void handleTouch(){

        if(touchRead(INPUT_PIN) < touch_sensibility){
                if(!pressed){

                    int touchstat = 0;                
                    for(int i=0; i< 20; i++){
                        touchstat = touchstat + touchRead(INPUT_PIN); //Cem leituras para confirmar que não é um falso positivo ou ruido
                        delay(5);                    
                    } 
                    
                    touchstat = touchstat / 20; // obtém a média    

                    if (touchstat <= touch_sensibility) {
                        //Serial.println("Pin 1: " + String(touchstat));                    
                        fauxmo.toggleState(0); 
                        pressed = true;
                    }                          
                }  
            }
        /*
        if(touchRead(INPUT_PIN) < touch_sensibility){
                if(!pressed){

                    int touchstat = 0;                
                    for(int i=0; i< 20; i++){
                        touchstat = touchstat + touchRead(INPUT_PIN); //Cem leituras para confirmar que não é um falso positivo ou ruido
                        delay(5);                    
                    } 
                    
                    touchstat = touchstat / 20; // obtém a média    

                    if (touchstat <= touch_sensibility) {
                        //Serial.println("Pin 1: " + String(touchstat));                    
                        fauxmo.setState(0,false,0); 
                        pressed = true;
                    }                          
                }  
            }
            else if(touchRead(INPUT_PIN2) < touch_sensibility){   
                if(!pressed){   
                    
                    int touchstat = 0;               
                    for(int i=0; i< 20; i++){
                        touchstat = touchstat + touchRead(INPUT_PIN2); //Cem leituras para confirmar que não é um falso positivo ou ruido
                        delay(5);  
                    } 
                    touchstat = touchstat / 20; // obtém a média
                    
                    if (touchstat <= touch_sensibility) {
                        //Serial.println("Pin 2: " + String(touchstat)); 
                        fauxmo.setState(0,true,1);
                        pressed = true;
                    }
                }
            }
            else if(touchRead(INPUT_PIN3) < touch_sensibility){  
                if(!pressed){     

                    int touchstat = 0;                
                    for(int i=0; i< 20; i++){
                        touchstat = touchstat + touchRead(INPUT_PIN3); //Cem leituras para confirmar que não é um falso positivo ou ruido
                        delay(5);                          
                    } 
                    touchstat = touchstat / 20; // obtém a média
                    
                    if (touchstat <= touch_sensibility) {
                        //Serial.println("Pin 3: " + String(touchstat)); 
                        fauxmo.setState(0,true,170);
                        pressed = true;
                    } 
                }
            }
            else if(touchRead(INPUT_PIN4) < touch_sensibility){ 
                if(!pressed){    

                    int touchstat = 0;
                    for(int i=0; i< 20; i++){
                        touchstat = touchstat + touchRead(INPUT_PIN4); //Cem leituras para confirmar que não é um falso positivo ou ruido
                        delay(5);  
                    } 
                    touchstat = touchstat / 20; // obtém a média
                    
                    if (touchstat <= touch_sensibility) {
                        //Serial.println("Pin 4: " + String(touchstat)); 
                        fauxmo.setState(0,true,255);
                        pressed = true;
                    } 
                }
            }*/
            else pressed = false;
    }
#else
void touchCalibration(){}

void handleTouch(){}
#endif

