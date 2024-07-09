#include <ArduinoOTA.h>

void initializeOTAService();
void startOTA();
void endOTA();
void progressOTA(unsigned int progress, unsigned int total);
void errorOTA(ota_error_t error);
