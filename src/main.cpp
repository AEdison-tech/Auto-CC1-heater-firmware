#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "ElegooCC.h"
#include "HeaterController.h"
#include "LittleFS.h"
#include "Logger.h"
#include "SettingsManager.h"
#include "WebServer.h"
#include "improv.h"
#include "time.h"

#define SPIFFS LittleFS

#ifndef FIRMWARE_VERSION_RAW
#define FIRMWARE_VERSION_RAW "dev"
#endif
#ifndef CHIP_FAMILY_RAW
#define CHIP_FAMILY_RAW "Unknown"
#endif

const char *firmwareVersion = FIRMWARE_VERSION_RAW;
const char *chipFamily      = CHIP_FAMILY_RAW;

#define WIFI_CHECK_INTERVAL    30000UL
#define WIFI_RECONNECT_TIMEOUT 10000UL
#define NTP_SYNC_INTERVAL      3600000UL

const char *ntpServer = "pool.ntp.org";

WebServer webServer(80);

unsigned long lastWifiCheck      = 0;
unsigned long wifiReconnectStart = 0;
bool          isReconnecting     = false;

bool isWifiSetup      = false;
bool isElegooSetup    = false;
bool isWebServerSetup = false;
bool isNtpSetup       = false;

uint8_t x_buffer[16];
uint8_t x_position = 0;

unsigned long lastNTPSyncAttempt = 0;

void failWifi()
{
    if (!settingsManager.getHasConnected())
    {
        settingsManager.setAPMode(true);
        if (settingsManager.save())
        {
            logger.log("WiFi failed, reverted to AP mode");
        }
        else
        {
            logger.log("WiFi failed, save error — restarting anyway");
        }
        delay(1000);
        ESP.restart();
    }
    else
    {
        logger.log("WiFi connection failed, retrying in 30 seconds");
        isReconnecting = false;
    }
}

void startAPMode()
{
    logger.log("Starting AP mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(10, 10, 10, 10), IPAddress(10, 10, 10, 10), IPAddress(255, 255, 255, 0));
    WiFi.softAP("ElegooChamberHeater", "elegoocc");
    MDNS.end();
    heaterController.setApMode(true);
}

void handleSuccessfulWifiConnection()
{
    logger.logf("WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
    isReconnecting = false;
    heaterController.setApMode(false);

    if (!settingsManager.getHasConnected())
    {
        settingsManager.setHasConnected(true);
        settingsManager.save();
        logger.log("First successful WiFi connection recorded");
    }

    MDNS.end();
    if (!MDNS.begin("ccheater"))
    {
        logger.log("mDNS start failed");
    }
}

bool connectToWifiStation(bool isReconnect = false)
{
    logger.logf("%s WiFi: %s", isReconnect ? "Reconnecting to" : "Connecting to",
                settingsManager.getSSID().c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(settingsManager.getSSID().c_str(), settingsManager.getPassword().c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30)
    {
        Serial.print('.');
        delay(1000);
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        handleSuccessfulWifiConnection();
        return true;
    }

    if (!isReconnect) failWifi();
    else logger.log("Failed to connect with new WiFi credentials");
    return false;
}

void cleanupWifiConnections()
{
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    delay(1000);
}

bool wifiSetup()
{
    if (settingsManager.isAPMode() || settingsManager.getSSID().isEmpty())
    {
        startAPMode();
        return false;
    }
    return connectToWifiStation(false);
}

bool reconnectWifiWithNewCredentials()
{
    logger.log("Applying new WiFi credentials...");
    cleanupWifiConnections();

    if (settingsManager.isAPMode())
    {
        startAPMode();
        return false;
    }
    return connectToWifiStation(true);
}

void checkWifiConnection()
{
    if (settingsManager.isAPMode()) return;

    if (WiFi.status() != WL_CONNECTED)
    {
        if (!isReconnecting)
        {
            logger.log("WiFi disconnected, attempting reconnect...");
            WiFi.begin(settingsManager.getSSID().c_str(),
                       settingsManager.getPassword().c_str());
            wifiReconnectStart = millis();
            isReconnecting     = true;
        }
        else if (millis() - wifiReconnectStart >= WIFI_RECONNECT_TIMEOUT)
        {
            failWifi();
        }
    }
    else if (isReconnecting)
    {
        logger.log("WiFi reconnected");
        isReconnecting = false;
        if (!settingsManager.getHasConnected())
        {
            settingsManager.setHasConnected(true);
            settingsManager.save();
        }
    }
}

void setup()
{
    Serial.begin(115200);

    heaterController.setup();  // configure output pins early

    logger.log("Chamber Heater starting...");
    logger.logf("Firmware: %s, Chip: %s", firmwareVersion, chipFamily);

    if (!LittleFS.begin(true))
    {
        logger.log("LittleFS mount failed");
    }
    else
    {
        logger.log("Filesystem initialized");
        settingsManager.load();
        logger.log("Settings loaded");
    }
}

void syncTimeWithNTP(unsigned long currentTime)
{
    lastNTPSyncAttempt = currentTime;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
        logger.log("NTP sync OK");
    else
        logger.log("NTP sync failed");
}

unsigned long getTime()
{
    time_t now;
    time(&now);
    return now;
}

// ---- improv-wifi serial provisioning ----

void onImprovErrorCallback(improv::Error err)
{
    logger.logf("Improv error: %d", err);
}

std::vector<std::string> getLocalUrl()
{
    return {String("http://" + WiFi.localIP().toString()).c_str()};
}

void getAvailableWifiNetworks()
{
    int n = WiFi.scanNetworks();
    for (int id = 0; id < n; ++id)
    {
        std::vector<uint8_t> data = improv::build_rpc_response(
            improv::GET_WIFI_NETWORKS,
            {WiFi.SSID(id), String(WiFi.RSSI(id)),
             (WiFi.encryptionType(id) == WIFI_AUTH_OPEN ? "NO" : "YES")},
            false);
        improv::send_response(data);
        delay(1);
    }
    std::vector<uint8_t> data =
        improv::build_rpc_response(improv::GET_WIFI_NETWORKS, std::vector<std::string>{}, false);
    improv::send_response(data);
}

bool onImprovCommandCallback(improv::ImprovCommand cmd)
{
    switch (cmd.command)
    {
        case improv::Command::GET_CURRENT_STATE:
            if (WiFi.status() == WL_CONNECTED)
            {
                improv::set_state(improv::STATE_PROVISIONED);
                auto data = improv::build_rpc_response(improv::GET_CURRENT_STATE, getLocalUrl(), false);
                improv::send_response(data);
            }
            else
            {
                improv::set_state(improv::STATE_AUTHORIZED);
            }
            break;

        case improv::Command::WIFI_SETTINGS:
            if (cmd.ssid.length() == 0)
            {
                improv::set_error(improv::ERROR_INVALID_RPC);
                break;
            }
            improv::set_state(improv::STATE_PROVISIONING);
            settingsManager.setSSID(cmd.ssid.c_str());
            settingsManager.setPassword(cmd.password.c_str());
            settingsManager.setAPMode(false);
            settingsManager.save(true);
            if (reconnectWifiWithNewCredentials())
            {
                improv::set_state(improv::STATE_PROVISIONED);
                auto data = improv::build_rpc_response(improv::WIFI_SETTINGS, getLocalUrl(), false);
                improv::send_response(data);
            }
            else
            {
                improv::set_state(improv::STATE_STOPPED);
                improv::set_error(improv::ERROR_UNABLE_TO_CONNECT);
            }
            break;

        case improv::Command::GET_DEVICE_INFO:
        {
            std::vector<std::string> infos = {"CC_Heater", firmwareVersion, chipFamily,
                                              "CC_Heater"};
            auto data = improv::build_rpc_response(improv::GET_DEVICE_INFO, infos, false);
            improv::send_response(data);
            break;
        }

        case improv::Command::GET_WIFI_NETWORKS:
            getAvailableWifiNetworks();
            break;

        default:
            improv::set_error(improv::ERROR_UNKNOWN_RPC);
            return false;
    }
    return true;
}

bool handleImprovWifi()
{
    if (Serial.available() > 0)
    {
        uint8_t b = Serial.read();
        if (parse_improv_serial_byte(x_position, b, x_buffer, onImprovCommandCallback,
                                     onImprovErrorCallback))
        {
            x_buffer[x_position++] = b;
        }
        else
        {
            x_position = 0;
        }
        return true;
    }
    return false;
}

// ---- main loop ----

void loop()
{
    if (handleImprovWifi()) return;

    unsigned long currentTime     = millis();
    bool          isWifiConnected = !settingsManager.isAPMode() && WiFi.status() == WL_CONNECTED;

    if (!isWifiSetup)
    {
        isWifiSetup = true;
        wifiSetup();
        logger.log("WiFi setup complete");
        return;
    }
    if (!isWebServerSetup)
    {
        webServer.begin();
        isWebServerSetup = true;
        logger.log("Web server started");
        return;
    }

    if (settingsManager.requestWifiReconnect)
    {
        settingsManager.requestWifiReconnect = false;
        reconnectWifiWithNewCredentials();
    }

    if (isWifiConnected)
    {
        if (!isElegooSetup)
        {
            elegooCC.setup();
            logger.log("Elegoo CC connected");
            isElegooSetup = true;
        }
        elegooCC.loop();

        if (!isNtpSetup)
        {
            configTime(0, 0, ntpServer);
            syncTimeWithNTP(currentTime);
            isNtpSetup = true;
        }
        else if (currentTime - lastNTPSyncAttempt >= NTP_SYNC_INTERVAL)
        {
            syncTimeWithNTP(currentTime);
        }
    }
    else if (currentTime - lastWifiCheck >= WIFI_CHECK_INTERVAL)
    {
        lastWifiCheck = currentTime;
        checkWifiConnection();
    }

    // Heater control — runs every loop regardless of WiFi state
    {
        bool  isPrinting  = false;
        float bedTemp     = 0.0f;
        float controlTemp = NAN;

        bool requirePrinting    = settingsManager.getRequirePrinting();
        bool useChamberControl  = settingsManager.getControlSource() == "chamber";

        if (settingsManager.getDebug())
        {
            isPrinting  = (!requirePrinting || settingsManager.debugIsPrinting) && settingsManager.getEnabled();
            bedTemp     = settingsManager.debugBedTemp;
            controlTemp = useChamberControl ? settingsManager.debugChamberTemp : NAN;
        }
        else if (isElegooSetup)
        {
            printer_info_t info = elegooCC.getCurrentInformation();
            isPrinting   = (!requirePrinting || info.isPrinting) && settingsManager.getEnabled();
            bedTemp      = info.bedTemp;
            controlTemp  = useChamberControl ? info.chamberTemp : NAN;
        }

        heaterController.loop(isPrinting, bedTemp, settingsManager.getActivationTemp(),
                               controlTemp, settingsManager.getTargetTemp(),
                               settingsManager.getHysteresis());
    }

    webServer.loop();
}
