#include "SettingsManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <stdlib.h>

#include "Logger.h"

SettingsManager &SettingsManager::getInstance()
{
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager()
{
    isLoaded             = false;
    requestWifiReconnect = false;
    wifiChanged          = false;
    debugBedTemp         = 0.0f;
    debugHeaterTemp      = 0.0f;
    debugChamberTemp     = 0.0f;
    debugIsPrinting      = false;

    settings.ap_mode           = true;
    settings.ssid              = "";
    settings.passwd            = "";
    settings.elegooip          = "";
    settings.activation_temp   = 70.0f;
    settings.control_source    = "heater";
    settings.require_printing  = false;
    settings.target_temp       = 60.0f;
    settings.hysteresis        = 5.0f;
    settings.enabled           = true;
    settings.has_connected     = false;
    settings.debug             = false;
}

bool SettingsManager::load()
{
    File file = LittleFS.open("/user_settings.json", "r");
    if (!file)
    {
        logger.log("Settings file not found, using defaults");
        isLoaded = true;
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError     error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        logger.log("Settings JSON parsing error, using defaults");
        isLoaded = true;
        return false;
    }

    settings.ap_mode           = doc["ap_mode"] | true;
    settings.ssid              = doc["ssid"] | "";
    settings.passwd            = doc["passwd"] | "";
    settings.elegooip          = doc["elegooip"] | "";
    settings.activation_temp   = doc["activation_temp"] | 70.0f;
    settings.control_source    = doc["control_source"] | "heater";
    settings.require_printing  = doc["require_printing"] | false;
    settings.target_temp       = doc["target_temp"] | 60.0f;
    settings.hysteresis        = doc["hysteresis"] | 5.0f;
    settings.enabled           = doc["enabled"] | true;
    settings.has_connected     = doc["has_connected"] | false;
    settings.debug             = doc["debug"] | false;

    isLoaded = true;
    return true;
}

bool SettingsManager::save(bool skipWifiCheck)
{
    String output = toJson(true);

    File file = LittleFS.open("/user_settings.json", "w");
    if (!file)
    {
        logger.log("Failed to open settings file for writing");
        return false;
    }

    if (file.print(output) == 0)
    {
        logger.log("Failed to write settings to file");
        file.close();
        return false;
    }

    file.close();
    logger.log("Settings saved");

    if (!skipWifiCheck && wifiChanged)
    {
        logger.log("WiFi changed, requesting reconnection");
        requestWifiReconnect = true;
        wifiChanged          = false;
    }
    return true;
}

const user_settings &SettingsManager::getSettings()
{
    if (!isLoaded) load();
    return settings;
}

String SettingsManager::getSSID()        { return getSettings().ssid; }
String SettingsManager::getPassword()    { return getSettings().passwd; }
bool   SettingsManager::isAPMode()       { return getSettings().ap_mode; }
String SettingsManager::getElegooIP()    { return getSettings().elegooip; }
float  SettingsManager::getActivationTemp()   { return getSettings().activation_temp; }
String SettingsManager::getControlSource()    { return getSettings().control_source; }
bool   SettingsManager::getRequirePrinting()  { return getSettings().require_printing; }
float  SettingsManager::getTargetTemp()       { return getSettings().target_temp; }
float  SettingsManager::getHysteresis()  { return getSettings().hysteresis; }
bool   SettingsManager::getEnabled()     { return getSettings().enabled; }
bool   SettingsManager::getHasConnected(){ return getSettings().has_connected; }
bool   SettingsManager::getDebug()       { return getSettings().debug; }

void SettingsManager::setSSID(const String &ssid)
{
    if (!isLoaded) load();
    if (settings.ssid != ssid) { settings.ssid = ssid; wifiChanged = true; }
}

void SettingsManager::setPassword(const String &password)
{
    if (!isLoaded) load();
    if (settings.passwd != password) { settings.passwd = password; wifiChanged = true; }
}

void SettingsManager::setAPMode(bool apMode)
{
    if (!isLoaded) load();
    if (settings.ap_mode != apMode) { settings.ap_mode = apMode; wifiChanged = true; }
}

void SettingsManager::setElegooIP(const String &ip)
{
    if (!isLoaded) load();
    settings.elegooip = ip;
}

void SettingsManager::setActivationTemp(float temp)
{
    if (!isLoaded) load();
    settings.activation_temp = temp;
}

void SettingsManager::setControlSource(const String &source)
{
    if (!isLoaded) load();
    settings.control_source = source;
}

void SettingsManager::setRequirePrinting(bool requirePrinting)
{
    if (!isLoaded) load();
    settings.require_printing = requirePrinting;
}

void SettingsManager::setTargetTemp(float temp)
{
    if (!isLoaded) load();
    settings.target_temp = temp;
}

void SettingsManager::setHysteresis(float hysteresis)
{
    if (!isLoaded) load();
    settings.hysteresis = hysteresis;
}

void SettingsManager::setEnabled(bool enabled)
{
    if (!isLoaded) load();
    settings.enabled = enabled;
}

void SettingsManager::setHasConnected(bool hasConnected)
{
    if (!isLoaded) load();
    settings.has_connected = hasConnected;
}

void SettingsManager::setDebug(bool debug)
{
    if (!isLoaded) load();
    settings.debug = debug;
}

String SettingsManager::toJson(bool includePassword)
{
    String                   output;
    StaticJsonDocument<1024> doc;

    doc["ap_mode"]           = settings.ap_mode;
    doc["ssid"]              = settings.ssid;
    doc["elegooip"]          = settings.elegooip;
    doc["activation_temp"]   = settings.activation_temp;
    doc["control_source"]    = settings.control_source;
    doc["require_printing"]  = settings.require_printing;
    doc["target_temp"]       = settings.target_temp;
    doc["hysteresis"]        = settings.hysteresis;
    doc["enabled"]           = settings.enabled;
    doc["has_connected"]     = settings.has_connected;
    doc["debug"]             = settings.debug;

    if (includePassword)
    {
        doc["passwd"] = settings.passwd;
    }

    serializeJson(doc, output);
    return output;
}
