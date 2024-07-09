#include <Arduino.h>

#include "fauxmoESP.h"

#include <EEPROM.h>
 // -----------------------------------------------------------------------------
// UDP
// -----------------------------------------------------------------------------

void fauxmoESP::_sendUDPResponse() {

    DEBUG_MSG_FAUXMO("[FAUXMO] Responding to M-SEARCH request\n");

    IPAddress ip = WiFi.localIP();
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();

    char response[strlen(FAUXMO_UDP_RESPONSE_TEMPLATE) + 128];
    snprintf_P(
        response, sizeof(response),
        FAUXMO_UDP_RESPONSE_TEMPLATE,
        ip[0], ip[1], ip[2], ip[3],
        _tcp_port,
        mac.c_str(), mac.c_str());

    DEBUG_MSG_FAUXMO(response);

    #if DEBUG_FAUXMO_VERBOSE_UDP
    DEBUG_MSG_FAUXMO("[FAUXMO] UDP response sent to %s:%d\n%s", _udp.remoteIP().toString().c_str(), _udp.remotePort(), response);
    #endif

    _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
    #if defined(ESP32)
    _udp.printf(response);
    #else
    _udp.write(response);
    #endif
    _udp.endPacket();
}

void fauxmoESP::_handleUDP() {

    int len = _udp.parsePacket();
    if (len > 0) {

        unsigned char data[len + 1];
        _udp.read(data, len);
        data[len] = 0;

        #if DEBUG_FAUXMO_VERBOSE_UDP
        DEBUG_MSG_FAUXMO("[FAUXMO] UDP packet received\n%s", (const char * ) data);
        #endif

        String request = (const char * ) data;
        if (request.indexOf("M-SEARCH") >= 0) {
            if ((request.indexOf("ssdp:discover") > 0) || (request.indexOf("upnp:rootdevice") > 0) || (request.indexOf("device:basic:1") > 0)) {
                _sendUDPResponse();
            }
        }
    }
}

// -----------------------------------------------------------------------------
// TCP
// -----------------------------------------------------------------------------

void fauxmoESP::_sendTCPResponse(AsyncClient * client,
    const char * code, char * body,
        const char * mime) {

    char headers[strlen_P(FAUXMO_TCP_HEADERS) + 32];
    snprintf_P(
        headers, sizeof(headers),
        FAUXMO_TCP_HEADERS,
        code, mime, strlen(body));

    #if DEBUG_FAUXMO_VERBOSE_TCP
    DEBUG_MSG_FAUXMO("[FAUXMO] Response:\n%s%s\n", headers, body);
    #endif

    client -> write(headers);
    client -> write(body);
}

String fauxmoESP::_deviceJson(unsigned char id, bool all = true) {

    if (id >= _devices.size()) return "{}";

    fauxmoesp_device_t device = _devices[id];

    DEBUG_MSG_FAUXMO("[FAUXMO] Sending device info for \"%s\", uniqueID = \"%s\"\n", device.name, device.uniqueid);
    char buffer[strlen_P(FAUXMO_DEVICE_JSON_TEMPLATE) + 64];

    if (all) {
        int timerMinutes;
        if (device.timerMillis == 0) timerMinutes = -1; //-1 is timer off
        else timerMinutes = (device.timerMillis - millis()) / 60000;

        snprintf_P(
            buffer, sizeof(buffer),
            FAUXMO_DEVICE_JSON_TEMPLATE,
            device.type,
            device.name, device.uniqueid,
            device.state ? "true" : "false",
            device.value,
            device.timerState ? "true" : "false",
            timerMinutes,
            startState ? "true" : "false"

        );
    } else {
        snprintf_P(
            buffer, sizeof(buffer),
            FAUXMO_DEVICE_JSON_TEMPLATE_SHORT,
            device.type, device.name, device.uniqueid);
    }

    return String(buffer);
}

String fauxmoESP::_byte2hex(uint8_t zahl) {
    String hstring = String(zahl, HEX);
    if (zahl < 16) {
        hstring = "0" + hstring;
    }

    return hstring;
}

String fauxmoESP::_makeMD5(String text) {
    unsigned char bbuf[16];
    String hash = "";
    MD5Builder md5;
    md5.begin();
    md5.add(text);
    md5.calculate();

    md5.getBytes(bbuf);
    for (uint8_t i = 0; i < 16; i++) {
        hash += _byte2hex(bbuf[i]);
    }

    return hash;
}

bool fauxmoESP::_onTCPDescription(AsyncClient * client, String url, String body) {

    (void) url;
    (void) body;

    DEBUG_MSG_FAUXMO("[FAUXMO] Handling /description.xml request\n");

    IPAddress ip = WiFi.localIP();
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();

    char response[strlen_P(FAUXMO_DESCRIPTION_TEMPLATE) + 64];
    snprintf_P(
        response, sizeof(response),
        FAUXMO_DESCRIPTION_TEMPLATE,
        ip[0], ip[1], ip[2], ip[3], _tcp_port,
        ip[0], ip[1], ip[2], ip[3], _tcp_port,
        mac.c_str(), mac.c_str());

    _sendTCPResponse(client, "200 OK", response, "text/xml");

    return true;
}

bool fauxmoESP::_onTCPList(AsyncClient * client, String url) {

    DEBUG_MSG_FAUXMO("[FAUXMO] Handling list request\n");

    // Get the index
    int pos = url.indexOf("lights");
    if (-1 == pos) return false;

    // Get the id
    unsigned char id = url.substring(pos + 7).toInt();

    // This will hold the response string
    String response;

    // Client is requesting all devices
    if (0 == id) {

        response += "{";
        for (unsigned char i = 0; i < _devices.size(); i++) {
            if (i > 0) response += ",";
            response += "\"" + String(i + 1) + "\":" + _deviceJson(i, false); // send short template
        }
        response += "}";

        // Client is requesting a single device
    } else response = _deviceJson(id - 1);

    _sendTCPResponse(client, "200 OK", (char * ) response.c_str(), "application/json");

    return true;
}

byte fauxmoESP::GetDeviceIndex(String * url) {
    int pos = url -> indexOf("lights");
    if (-1 == pos) return 0;

    //find the "lights/id"
    byte id = url -> substring(pos + 7).toInt();
    if (id > 0) return id;

    return 0;
}

bool fauxmoESP::_onTCPControl(AsyncClient * client, String url, String body) {

    // "devicetype" request
    if (body.indexOf("devicetype") > 0) {
        DEBUG_MSG_FAUXMO("[FAUXMO] Handling devicetype request\n");
        _sendTCPResponse(client, "200 OK", (char * )
            "[{\"success\":{\"username\": \"2WLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBQr\"}}]", "application/json");
        return true;
    }

    if (body.length() > 0) {

        //get device id
        byte id = GetDeviceIndex( & url);

        if (id > 0) {

            id--;
            // "state" request
            if (url.indexOf("state") > 0) {
                DEBUG_MSG_FAUXMO("[FAUXMO] Handling state request\n");

                // Brightness
                int pos = body.indexOf("bri");
                if (pos > 0) {
                    unsigned char value = body.substring(pos + 5).toInt();
                    _devices[id].value = value;
                    
                    if(strcmp(_devices[id].type, "VENTILADOR") == 0){
                        if (body.indexOf("true") > 0) _devices[id].state = true;
                        else _devices[id].state = false;
                    }
                    else _devices[id].state = (value > 0);
                    

                }
                // Only state
                else if (body.indexOf("true") > 0) {
                    _devices[id].state = true;
                    if (0 == _devices[id].value && !strcmp(_devices[id].type, "VENTILADOR") == 0) _devices[id].value = 255;
                } else
                    _devices[id].state = false;

                char response[strlen_P(FAUXMO_TCP_STATE_RESPONSE) + 10];
                snprintf_P(
                    response, sizeof(response),
                    FAUXMO_TCP_STATE_RESPONSE,
                    id + 1, _devices[id].state ? "true" : "false", id + 1, _devices[id].value);
                _sendTCPResponse(client, "200 OK", response, "text/xml");

                if (_setCallback) _setCallback(id, _devices[id].name, _devices[id].state, _devices[id].value, _devices[id].pin);

                return true;
            }

            // "timer" request
            else if (url.indexOf("timer") > 0) {
                DEBUG_MSG_FAUXMO("[FAUXMO] Handling timer request\n");

                //Search for the minutes in the body {"timerState": true, "minutes": 10}
                int pos = body.indexOf("minutes");
                if (pos > 0) {
                    int value = body.substring(pos + 9).toFloat();
                    if (value == -1) {
                        _devices[id].timerMillis = 0;
                        DEBUG_MSG_FAUXMO("[FAUXMO][TIMER] Timer off...");
                    } else {
                        _devices[id].timerMillis = millis() + value * 60000;
                        DEBUG_MSG_FAUXMO("[FAUXMO][TIMER] Timer on...");
                    }
                } else return false; //Caso não encontre, retorna

                //Busca o estado no corpo da requisição
                if (body.indexOf("false") > 0) _devices[id].timerState = false;
                else _devices[id].timerState = true;

                char response[strlen_P(FAUXMO_TCP_TIMER_RESPONSE) + 10];
                snprintf_P(
                    response, sizeof(response),
                    FAUXMO_TCP_TIMER_RESPONSE,
                    id + 1, _devices[id].timerState ? "true" : "false", id + 1,
                    ((int)(_devices[id].timerMillis - millis()) / 60000));
                _sendTCPResponse(client, "200 OK", response, "text/xml");

                return true;
            }

            // "rename" request
            else if (url.indexOf("rename") > 0) {
                DEBUG_MSG_FAUXMO("[FAUXMO] Handling rename request\n");

                //Search for the name in the body {"name":"TEST"}
                int pos = body.indexOf("name");
                if (pos > 0) {
                    String name = body.substring(pos + 7, body.indexOf("\"", pos + 7));
                    renameDevice(id, name, client);
                }

                return true;
            }

            // "startState" request
            else if (url.indexOf("startState") > 0) {
                DEBUG_MSG_FAUXMO("[FAUXMO] Handling startState request\n");

                //Search for the state in the body {"state":"true"}
                int pos = body.indexOf("true");

                if (pos > 0) setStartState(true);
                else setStartState(false);

                _sendTCPResponse(client, "200 OK", (char*) "Sucesso!", "text/xml");

                return true;
            }

            else if ((url.indexOf("reset") > 0) && (body.length() > 0)) {
                int pos = body.indexOf("key");
                if (pos > 0) {
                    //Default body {"key":"f635g2vg3gn54H$"}                    
                    String key = body.substring(pos + 6, 27);
                    if (key == "f635g2vg3gn54H$") {
                        
                        _sendTCPResponse(client, "200 OK", (char*) "Sucesso!", "text/xml");
                        delay(1000);
                        _resetDevice();
                    }
                    else _sendTCPResponse(client, "200 OK", (char*) "Chave incorreta!", "text/xml");
                }
            }
        }
    }
    return false;
}
bool fauxmoESP::_onTCPRequest(AsyncClient * client, bool isGet, String url, String body) {

    if (!_enabled) return false;

    #if DEBUG_FAUXMO_VERBOSE_TCP
    DEBUG_MSG_FAUXMO("[FAUXMO] isGet: %s\n", isGet ? "true" : "false");
    DEBUG_MSG_FAUXMO("[FAUXMO] URL: %s\n", url.c_str());
    if (!isGet) DEBUG_MSG_FAUXMO("[FAUXMO] Body:\n%s\n", body.c_str());
    #endif

    if (url.equals("/description.xml")) {
        return _onTCPDescription(client, url, body);
    }

    if (url.startsWith("/api")) {
        if (isGet) {
            return _onTCPList(client, url);
        } else {
            return _onTCPControl(client, url, body);
        }
    }

    return false;
}

bool fauxmoESP::_onTCPData(AsyncClient * client, void * data, size_t len) {

    if (!_enabled) return false;

    char * p = (char * ) data;
    p[len] = 0;

    #if DEBUG_FAUXMO_VERBOSE_TCP
    DEBUG_MSG_FAUXMO("[FAUXMO] TCP request\n%s\n", p);
    #endif

    // Method is the first word of the request
    char * method = p;

    while ( * p != ' ') p++;
    * p = 0;
    p++;

    // Split word and flag start of url
    char * url = p;

    // Find next space
    while ( * p != ' ') p++;
    * p = 0;
    p++;

    // Find double line feed
    unsigned char c = 0;
    while (( * p != 0) && (c < 2)) {
        if ( * p != '\r') {
            c = ( * p == '\n') ? c + 1 : 0;
        }
        p++;
    }
    //char *body = p;

    bool isGet = (strncmp(method, "GET", 3) == 0);
    return _onTCPRequest(client, isGet, url, p);
}

void fauxmoESP::_onTCPClient(AsyncClient * client) {

    if (_enabled) {

        for (unsigned char i = 0; i < FAUXMO_TCP_MAX_CLIENTS; i++) {

            if (!_tcpClients[i] || !_tcpClients[i] -> connected()) {

                _tcpClients[i] = client;

                client -> onAck([i](void * s, AsyncClient * c, size_t len, uint32_t time) {},
                    0);

                client -> onData([this, i](void * s, AsyncClient * c, void * data, size_t len) {
                        _onTCPData(c, data, len);
                    },
                    0);
                client -> onDisconnect([this, i](void * s, AsyncClient * c) {
                        if (_tcpClients[i] != NULL) {
                            _tcpClients[i] -> free();
                            _tcpClients[i] = NULL;
                        } else {
                            DEBUG_MSG_FAUXMO("[FAUXMO] Client %d already disconnected\n", i);
                        }
                        delete c;
                        DEBUG_MSG_FAUXMO("[FAUXMO] Client #%d disconnected\n", i);
                    },
                    0);

                client -> onError([i](void * s, AsyncClient * c, int8_t error) {
                        DEBUG_MSG_FAUXMO("[FAUXMO] Error %s (%d) on client #%d\n", c -> errorToString(error), error, i);
                    },
                    0);

                client -> onTimeout([i](void * s, AsyncClient * c, uint32_t time) {
                        DEBUG_MSG_FAUXMO("[FAUXMO] Timeout on client #%d at %i\n", i, time);
                        c -> close();
                    },
                    0);

                client -> setRxTimeout(FAUXMO_RX_TIMEOUT);

                DEBUG_MSG_FAUXMO("[FAUXMO] Client #%d connected\n", i);
                return;
            }
        }

        DEBUG_MSG_FAUXMO("[FAUXMO] Rejecting - Too many connections\n");

    } else {
        DEBUG_MSG_FAUXMO("[FAUXMO] Rejecting - Disabled\n");
    }

    client -> onDisconnect([](void * s, AsyncClient * c) {
        c -> free();
        delete c;
    });
    client -> close(true);
}

// -----------------------------------------------------------------------------
// Devices
// -----------------------------------------------------------------------------

fauxmoESP::~fauxmoESP() {

    // Free the name for each device
    for (auto & device: _devices) {
        free(device.name);
    }

    // Delete devices
    _devices.clear();
}

void fauxmoESP::setDeviceUniqueId(unsigned char id,
    const char * uniqueid) {
    strncpy(_devices[id].uniqueid, uniqueid, FAUXMO_DEVICE_UNIQUE_ID_LENGTH);
}

unsigned char fauxmoESP::addDevice(char * device_name,
    const char * type, byte pin) {

    fauxmoesp_device_t device;
    unsigned int device_id = _devices.size();

    // init properties
    device.name = strdup(device_name);
    device.type = type;
    device.state = false;
    device.value = 0;
    device.pin = pin;

    // create the uniqueid
    String mac = WiFi.macAddress();

    snprintf(device.uniqueid, 27, "%s:%s-%02X", mac.c_str(), "00:00", device_id);

    // Attach
    _devices.push_back(device);

    DEBUG_MSG_FAUXMO("[FAUXMO] Device '%s' added as #%d\n", device_name, device_id);

    return device_id;
}

int fauxmoESP::getDeviceId(char * device_name) {
    for (unsigned int id = 0; id < _devices.size(); id++) {
        if (strcmp(_devices[id].name, device_name) == 0) {
            return id;
        }
    }
    return -1;
}

bool fauxmoESP::renameDevice(unsigned char id, String device_name, AsyncClient * client) {

    if (id < _devices.size()) {
        //free(_devices[id].name);  
        //_devices[id].name = strdup(device_name);
        //strcpy(_devices[id].name, device_name.c_str());

        //Cada dispositivo possui um espaço de 16 bytes para armazenar seu nome
        // TAMANHO X (ID[começa em 0] + 1) - TAMANHO
        // EX: 16 * (0 + 1) - 16 = 0
        // EX2: 16 * (1 + 1) -16 = 16
        // EX3: 16 * (2 + 1) - 16 = 32

        int startAdress = 16 * (id + 1) - 16;
        EEPROM.begin(512);

        if (device_name.length() > EEPROM.length() || device_name.length() > 16 || device_name == "") { // verificamos se a string cabe na memória a partir do endereço desejado
            _sendTCPResponse(client, "400", (char*) "Nome muito grande!", "text/xml");
            return false;
            //Serial.println ("A sua String não cabe na EEPROM"); // Caso não caiba mensagem de erro é mostrada
        }

        #ifdef ESP32
        EEPROM.writeString(startAdress, device_name);

        #else
        for (int i = 0; i < device_name.length(); i++) {
            EEPROM.write(startAdress, device_name[i]); // Escrevemos cada byte da string de forma sequencial na memória
            startAdress++; // Deslocamos endereço base em uma posição a cada byte salvo
        }
        EEPROM.write(startAdress, '\0'); // Salvamos marcador de fim da string 
        #endif

        EEPROM.commit();
        EEPROM.end();

        _sendTCPResponse(client, "200 OK",(char*) "Dispositivo renomeado com sucesso!", "text/xml");
        DEBUG_MSG_FAUXMO("[FAUXMO] Device #%d renamed to '%s'\n", id, & device_name);
        
        //Delay para dar tempo de enviar a resposta
        delay(3000);
        #ifdef ESP32
        ESP.restart();
        #else
        ESP.reset();
        #endif

        return true;
    }

    return false;
}

void fauxmoESP::setStartState(bool state) {

    EEPROM.begin(512);
    EEPROM.write(500, state);
    EEPROM.commit();
    EEPROM.end();
    startState = state; 
    DEBUG_MSG_FAUXMO("Status de inicialização alterado com sucesso!");

    return;
    
}

/*
bool fauxmoESP::renameDevice(const char * old_device_name, const char * new_device_name) {
	int id = getDeviceId(old_device_name);
	if (id < 0) return false;
	return renameDevice(id, new_device_name);
}

bool fauxmoESP::removeDevice(unsigned char id) {
    if (id < _devices.size()) {
        free(_devices[id].name);
		_devices.erase(_devices.begin()+id);
        DEBUG_MSG_FAUXMO("[FAUXMO] Device #%d removed\n", id);
        return true;
    }
    return false;
}

bool fauxmoESP::removeDevice(const char * device_name) {
	int id = getDeviceId(device_name);
	if (id < 0) return false;
	return removeDevice(id);
}
*/
char * fauxmoESP::getDeviceName(unsigned char id, char * device_name, size_t len) {

    if ((id < _devices.size()) && (device_name != NULL))
        strncpy(device_name, _devices[id].name, len);

    return device_name;
}

bool fauxmoESP::setState(byte id, bool state, unsigned char value) {
    if (id <= _devices.size()) {
        _devices[id].state = state;
        _devices[id].value = value;
        _setCallback(id, _devices[id].name, _devices[id].state, _devices[id].value, _devices[id].pin);
        return true;
    }
    return false;
}

bool fauxmoESP::toggleState(byte id) {
    byte value;
    bool state;
    if (id <= _devices.size()) {
        state = !_devices[id].state;

        if (state) value = 255;
        else value = 0;

        setState(id, state, value);

        return true;
    }
    return false;
}

/*
bool fauxmoESP::setState(char * device_name, bool state, unsigned char value) {
	int id = getDeviceId(device_name);
	if (id < 0) return false;
	_devices[id].state = state;
	_devices[id].value = value;
	return true;
}
*/
// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool fauxmoESP::process(AsyncClient * client, bool isGet, String url, String body) {
    return _onTCPRequest(client, isGet, url, body);
}

void fauxmoESP::handle() {
    if (_enabled) {
        _handleUDP();
        handleTimer();
    }
}

void fauxmoESP::handleTimer() {

    for (byte id = 0; id < _devices.size(); id++) {
        if (_devices[id].timerMillis != 0) {

            if (_devices[id].timerMillis == millis()) {
                
                if(!strcmp(_devices[id].type, "VENTILADOR") == 0){
                    if(_devices[id].timerState) _devices[id].value = 255;
                    else  _devices[id].value = 0;   
                }
                           
                _setCallback(id, _devices[id].name, _devices[id].timerState, _devices[id].timerValue, _devices[id].pin);
                _devices[id].timerMillis = 0;
            }
        }
    }
}

void fauxmoESP::enable(bool enable) {

    if (enable == _enabled) return;
    _enabled = enable;

    if (_enabled) {
        DEBUG_MSG_FAUXMO("[FAUXMO] Enabled\n");
    } else {
        DEBUG_MSG_FAUXMO("[FAUXMO] Disabled\n")
    }

    if (_enabled) {

        // Start TCP server if internal
        if (_internal) {
            if (NULL == _server) {
                _server = new AsyncServer(_tcp_port);
                _server -> onClient([this](void * s, AsyncClient * c) {
                        _onTCPClient(c);
                    },
                    0);
            }
            _server -> begin();
        }
        
        // UDP setup
        #ifdef ESP32
        _udp.beginMulticast(FAUXMO_UDP_MULTICAST_IP, FAUXMO_UDP_MULTICAST_PORT);
        #else
        _udp.beginMulticast(WiFi.localIP(), FAUXMO_UDP_MULTICAST_IP, FAUXMO_UDP_MULTICAST_PORT);
        #endif
        DEBUG_MSG_FAUXMO("[FAUXMO] UDP server started\n");
    }
    else{
       //_udp.stop();
        _server -> end();
        _server = NULL;
        _udp.endPacket();    
    }
}