#ifndef HEATER_CONTROLLER_H
#define HEATER_CONTROLLER_H

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#ifndef RELAY_PIN
#define RELAY_PIN 2
#endif
#ifndef FAN_PIN
#define FAN_PIN 3
#endif
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 8
#endif
#ifndef TEMP_SENSOR_PIN
#define TEMP_SENSOR_PIN 4
#endif

#define FAN_COOLDOWN_MS    30000UL
#define LED_BLINK_MS       500UL
#define TEMP_CONVERSION_MS 800UL

class HeaterController
{
   private:
    bool             heaterActive;
    bool             fanActive;
    bool             apMode;
    bool             ledState;
    unsigned long    fanCooldownStart;
    unsigned long    lastLedToggle;
    float            heaterTemp;
    OneWire          oneWire;
    DallasTemperature sensors;
    unsigned long    lastTempRequest;

    HeaterController();
    HeaterController(const HeaterController &) = delete;
    HeaterController &operator=(const HeaterController &) = delete;

    void setRelay(bool on);
    void setFan(bool on);
    void setLed(bool on);
    void updateLed();

   public:
    static HeaterController &getInstance();

    void setup();
    void setApMode(bool ap);

    // Called every loop iteration.
    // isPrinting + printerTemp determine if conditions for heating are met.
    // controlTemp: external temperature for on/off control (NAN = use internal DS18B20).
    void loop(bool isPrinting, float printerTemp, float activationTemp, float controlTemp,
              float targetTemp, float hysteresis);

    float readHeaterTemp();
    bool  isHeaterActive() const;
    bool  isFanActive() const;
    float getHeaterTemp() const;
};

#define heaterController HeaterController::getInstance()

#endif  // HEATER_CONTROLLER_H
