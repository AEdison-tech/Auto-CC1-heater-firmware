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
      lastTempRequest(0),
      lastValidTemp(0),
      heatingAllowed(false),
      targetReached(false),
      lastTarget(NAN)
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

void HeaterController::updateSensor()
{
    if (settingsManager.getDebug())
    {
        heaterTemp    = settingsManager.debugHeaterTemp;
        lastValidTemp = millis();
        return;
    }

    unsigned long now = millis();
    if (now - lastTempRequest >= TEMP_CONVERSION_MS)
    {
        float t = sensors.getTempCByIndex(0);
        if (t != DEVICE_DISCONNECTED_C)
        {
            heaterTemp    = t;
            lastValidTemp = now;
        }
        sensors.requestTemperatures();
        lastTempRequest = now;
    }
}

sensor_state_t HeaterController::getSensorState() const
{
    if (settingsManager.getDebug())
        return SENSOR_OK;
    unsigned long now = millis();
    if (lastValidTemp == 0)
        return now < SENSOR_STALE_MS ? SENSOR_PENDING : SENSOR_FAILED;
    return now - lastValidTemp <= SENSOR_STALE_MS ? SENSOR_OK : SENSOR_FAILED;
}

void HeaterController::loop(bool allowHeating, float controlTemp, float targetTemp,
                             float hysteresis)
{
    if (allowHeating && !isnan(controlTemp))
    {
        // A new target (or heating just allowed) first heats up to the target;
        // the hysteresis band only applies once the target has been reached.
        // Otherwise a chamber starting inside the band would never be heated
        // and a wait for the target (M191) could stall.
        if (!heatingAllowed || targetTemp != lastTarget)
        {
            heatingAllowed = true;
            lastTarget     = targetTemp;
            targetReached  = false;
        }
        if (controlTemp >= targetTemp)
            targetReached = true;

        bool belowBand = controlTemp <= targetTemp - hysteresis;
        bool approach  = !targetReached && controlTemp < targetTemp;
        if (!heaterActive && (belowBand || approach))
        {
            logger.logf("Heater ON: %.1fC, target %.1fC (%s)", controlTemp, targetTemp,
                        belowBand ? "below target-hyst" : "heating up to target");
            setRelay(true);
            setFan(true);
            fanCooldownStart = 0;
        }
        else if (heaterActive && controlTemp >= targetTemp + hysteresis)
        {
            logger.logf("Heater OFF: %.1fC >= %.1fC (target+hyst)", controlTemp,
                        targetTemp + hysteresis);
            setRelay(false);
            fanCooldownStart = millis();
        }
    }
    else
    {
        heatingAllowed = false;
        if (heaterActive)
        {
            logger.log("Heater OFF: heating no longer allowed");
            setRelay(false);
            fanCooldownStart = millis();
        }
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
