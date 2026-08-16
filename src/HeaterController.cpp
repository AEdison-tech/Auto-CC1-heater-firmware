#include "HeaterController.h"

#include <cmath>

#include "Logger.h"
#include "SettingsManager.h"

HeaterController &HeaterController::getInstance()
{
    static HeaterController instance;
    return instance;
}

HeaterController::HeaterController()
    : heaterActive(false),
      fanActive(false),
      apMode(false),
      ledState(false),
      fanCooldownStart(0),
      lastLedToggle(0),
      heaterTemp(0.0f),
      oneWire(TEMP_SENSOR_PIN),
      sensors(&oneWire),
      lastTempRequest(0)
{
}

void HeaterController::setup()
{
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(FAN_PIN, OUTPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);

    setRelay(false);
    setFan(false);
    setLed(false);

#if STATUS_LED_PIN != 8
    // Internal LED on pin 8 is unused — force it off
    pinMode(8, OUTPUT);
    digitalWrite(8, LOW);
#endif

    sensors.begin();
    sensors.setResolution(12);
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();
    lastTempRequest = millis();

    logger.log("Heater controller initialized");
}

void HeaterController::setRelay(bool on)
{
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    heaterActive = on;
}

void HeaterController::setFan(bool on)
{
    digitalWrite(FAN_PIN, on ? HIGH : LOW);
    fanActive = on;
}

void HeaterController::setLed(bool on)
{
    ledState = on;
    digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);  // active-low (inverted)
}

void HeaterController::setApMode(bool ap)
{
    apMode = ap;
}

void HeaterController::updateLed()
{
    if (apMode)
    {
        unsigned long now = millis();
        if (now - lastLedToggle >= LED_BLINK_MS)
        {
            setLed(!ledState);
            lastLedToggle = now;
        }
    }
    else
    {
        setLed(heaterActive);
    }
}

float HeaterController::readHeaterTemp()
{
    if (settingsManager.getDebug())
        return settingsManager.debugHeaterTemp;

    unsigned long now = millis();
    if (now - lastTempRequest >= TEMP_CONVERSION_MS)
    {
        float t = sensors.getTempCByIndex(0);
        if (t != DEVICE_DISCONNECTED_C)
            heaterTemp = t;
        sensors.requestTemperatures();
        lastTempRequest = now;
    }
    return heaterTemp;
}

void HeaterController::loop(bool isPrinting, float printerTemp, float activationTemp,
                             float controlTemp, float targetTemp, float hysteresis)
{
    heaterTemp = readHeaterTemp();

    float effectiveTemp = isnan(controlTemp) ? heaterTemp : controlTemp;

    bool conditionsMet = isPrinting && (printerTemp >= activationTemp);

    if (conditionsMet)
    {
        if (!heaterActive && effectiveTemp <= targetTemp - hysteresis)
        {
            logger.logf("Heater ON: %.1fC < %.1fC (target-hyst)", effectiveTemp,
                        targetTemp - hysteresis);
            setRelay(true);
            setFan(true);
            fanCooldownStart = 0;
        }
        else if (heaterActive && effectiveTemp >= targetTemp + hysteresis)
        {
            logger.logf("Heater OFF: %.1fC >= %.1fC (target+hyst)", effectiveTemp,
                        targetTemp + hysteresis);
            setRelay(false);
            fanCooldownStart = millis();
        }
    }
    else if (heaterActive)
    {
        logger.log("Heater OFF: print ended or printer temp below activation threshold");
        setRelay(false);
        fanCooldownStart = millis();
    }

    // Fan cooldown — runs 30s after heater turns off
    if (!heaterActive && fanActive && fanCooldownStart > 0)
    {
        if (millis() - fanCooldownStart >= FAN_COOLDOWN_MS)
        {
            logger.log("Fan cooldown complete");
            setFan(false);
            fanCooldownStart = 0;
        }
    }

    updateLed();
}

bool HeaterController::isHeaterActive() const
{
    return heaterActive;
}

bool HeaterController::isFanActive() const
{
    return fanActive;
}

float HeaterController::getHeaterTemp() const
{
    return heaterTemp;
}
