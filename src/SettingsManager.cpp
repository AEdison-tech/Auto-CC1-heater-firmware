#include "SettingsManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
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
    klipperProfile       = false;
    requestWifiReconnect = false;
    requestRestart       = false;
    wifiChanged          = false;
    debugBedTemp         = 0.0f;
    debugHeaterTemp      = 0.0f;
    debugChamberTemp     = 0.0f;
    debugIsPrinting      = false;

    settings.ap_mode           = true;
    settings.ssid              = "";
    settings.passwd            = "";
    settings.elegooip          = "";
    settings.activation_temp   = 85.0f;
    settings.control_source    = "heater";
    settings.require_printing  = false;
    settings.target_temp       = 60.0f;
    settings.hysteresis        = 5.0f;
    settings.enabled           = true;
    settings.has_connected     = false;
    settings.debug             = false;
    settings.printer_profile   = PROFILE_ELEGOO;
    settings.max_chamber_temp  = 65.0f;
}

// The settings JSON is mirrored into NVS so that a filesystem (LittleFS) OTA
// update, which replaces /user_settings.json, does not wipe WiFi and heater
// settings: when the file is missing on boot it is restored from this copy.
String SettingsManager::loadBackup()
{
    Preferences prefs;
    if (!prefs.begin("cch", true))
        return "";
    String json = prefs.getString("settings", "");
    prefs.end();
    return json;
}

void SettingsManager::storeBackup(const String &json)
{
    Preferences prefs;
    if (!prefs.begin("cch", false))
    {
        logger.log("Settings backup: NVS open failed");
        return;
    }
    if (prefs.getString("settings", "") != json)
    {
        if (prefs.putString("settings", json) == 0)
            logger.log("Settings backup: NVS write failed");
        else
            logger.log("Settings backup stored in NVS");
    }
    prefs.end();
}

void SettingsManager::applyJson(JsonDocument &doc)
{
    settings.ap_mode           = doc["ap_mode"] | true;
    settings.ssid              = doc["ssid"] | "";
    settings.passwd            = doc["passwd"] | "";
    settings.elegooip          = doc["elegooip"] | "";
    settings.activation_temp   = doc["activation_temp"] | 85.0f;
    settings.control_source    = doc["control_source"] | "heater";
    settings.require_printing  = doc["require_printing"] | false;
    settings.target_temp       = doc["target_temp"] | 60.0f;
    settings.hysteresis        = doc["hysteresis"] | 5.0f;
    settings.enabled           = doc["enabled"] | true;
    settings.has_connected     = doc["has_connected"] | false;
    settings.debug             = doc["debug"] | false;
    settings.printer_profile   = doc["printer_profile"] | PROFILE_ELEGOO;
    settings.max_chamber_temp  = doc["max_chamber_temp"] | 65.0f;
    if (settings.printer_profile != PROFILE_KLIPPER)
        settings.printer_profile = PROFILE_ELEGOO;
    klipperProfile = settings.printer_profile == PROFILE_KLIPPER;
}

bool SettingsManager::load()
{
    StaticJsonDocument<1024> doc;

    File file = LittleFS.open("/user_settings.json", "r");
    if (!file)
    {
        String backup = loadBackup();
        if (backup.length() > 0 && !deserializeJson(doc, backup))
        {
            applyJson(doc);
            isLoaded = true;
            logger.log("Settings file missing, restored from NVS backup");
            save(true);
            return true;
        }
        logger.log("Settings file not found, using defaults");
        isLoaded = true;
        return false;
    }

    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        logger.log("Settings JSON parsing error, using defaults");
        isLoaded = true;
        return false;
    }

    applyJson(doc);
    isLoaded = true;
    storeBackup(toJson(true));
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
    storeBackup(output);

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
String SettingsManager::getPrinterProfile()      { return getSettings().printer_profile; }
bool   SettingsManager::isKlipperProfile() const { return klipperProfile; }
float  SettingsManager::getMaxChamberTemp()      { return getSettings().max_chamber_temp; }

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

void SettingsManager::setPrinterProfile(const String &profile)
{
    if (!isLoaded) load();
    settings.printer_profile = profile == PROFILE_KLIPPER ? PROFILE_KLIPPER : PROFILE_ELEGOO;
    klipperProfile           = settings.printer_profile == PROFILE_KLIPPER;
}

void SettingsManager::setMaxChamberTemp(float temp)
{
    if (!isLoaded) load();
    settings.max_chamber_temp = constrain(temp, 20.0f, 80.0f);
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
    doc["printer_profile"]   = settings.printer_profile;
    doc["max_chamber_temp"]  = settings.max_chamber_temp;

    if (includePassword)
    {
        doc["passwd"] = settings.passwd;
    }

    serializeJson(doc, output);
    return output;
}
