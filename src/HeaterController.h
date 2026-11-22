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

#define FAN_COOLDOWN_MS     30000UL
#define LED_BLINK_MS        500UL
#define TEMP_CONVERSION_MS  800UL
#define SENSOR_STALE_MS     5000UL  // no valid DS18B20 reading for this long = failed

typedef enum
{
    SENSOR_PENDING = 0,  // no reading yet (shortly after boot)
    SENSOR_OK      = 1,
    SENSOR_FAILED  = 2,
} sensor_state_t;

class HeaterController
{
   private:
    bool              heaterActive;
    bool              fanActive;
    bool              apMode;
    bool              ledState;
    unsigned long     fanCooldownStart;
    unsigned long     lastLedToggle;
    float             heaterTemp;
    OneWire           oneWire;
    DallasTemperature sensors;
    unsigned long     lastTempRequest;
    unsigned long     lastValidTemp;
    bool              heatingAllowed;  // allowHeating seen in the previous loop
    bool              targetReached;   // control temp reached the current target
    float             lastTarget;

    HeaterController();
    HeaterController(const HeaterController &)            = delete;
    HeaterController &operator=(const HeaterController &) = delete;

    void setRelay(bool on);
    void setFan(bool on);
    void setLed(bool on);
    void updateLed();

   public:
    static HeaterController &getInstance();

    void setup();
    void setApMode(bool ap);

    // Non-blocking DS18B20 update, call every loop iteration before loop().
    void updateSensor();

    // Hysteresis control. allowHeating gates the heater (activation conditions,
    // link, faults); controlTemp is compared against targetTemp ± hysteresis.
    void loop(bool allowHeating, float controlTemp, float targetTemp, float hysteresis);

    bool           isHeaterActive() const;
    bool           isFanActive() const;
    float          getHeaterTemp() const;
    sensor_state_t getSensorState() const;
};

#define heaterController HeaterController::getInstance()

#endif  // HEATER_CONTROLLER_H
