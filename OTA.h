#include <ArduinoOTA.h>

void initializeOTAService();
void stopOTAService();
void startOTA();
void progressOTA(unsigned int progress, unsigned int total);
void errorOTA(ota_error_t error);
