#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef SETTINGS_DATA_H
#define SETTINGS_DATA_H

struct user_settings
{
    String ssid;
    String passwd;
    bool   ap_mode;
    String elegooip;
    float  activation_temp;      // temperature threshold to start heating
    String control_source;       // "heater" (DS18B20) or "chamber" (TempOfBox) for on/off control
    bool   require_printing;     // require active print before enabling heater
    float  target_temp;          // desired box temperature
    float  hysteresis;           // on/off band (°C)
    bool   enabled;
    bool   has_connected;
    bool   debug;
};

class SettingsManager
{
   private:
    user_settings settings;
    bool          isLoaded;
    bool          wifiChanged;

    SettingsManager();
    SettingsManager(const SettingsManager &)            = delete;
    SettingsManager &operator=(const SettingsManager &) = delete;

   public:
    static SettingsManager &getInstance();

    bool  requestWifiReconnect;
    float debugBedTemp;
    float debugHeaterTemp;
    float debugChamberTemp;
    bool  debugIsPrinting;

    bool load();
    bool save(bool skipWifiCheck = false);

    const user_settings &getSettings();

    String getSSID();
    String getPassword();
    bool   isAPMode();
    String getElegooIP();
    float  getActivationTemp();
    String getControlSource();
    bool   getRequirePrinting();
    float  getTargetTemp();
    float  getHysteresis();
    bool   getEnabled();
    bool   getHasConnected();
    bool   getDebug();

    void setSSID(const String &ssid);
    void setPassword(const String &password);
    void setAPMode(bool apMode);
    void setElegooIP(const String &ip);
    void setActivationTemp(float temp);
    void setControlSource(const String &source);
    void setRequirePrinting(bool requirePrinting);
    void setTargetTemp(float temp);
    void setHysteresis(float hysteresis);
    void setEnabled(bool enabled);
    void setHasConnected(bool hasConnected);
    void setDebug(bool debug);

    String toJson(bool includePassword = true);
};

#define settingsManager SettingsManager::getInstance()

#endif
