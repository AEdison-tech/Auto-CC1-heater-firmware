#include "WebServer.h"

#include <AsyncJson.h>
#include <WiFi.h>

#include "ElegooCC.h"
#include "HeaterController.h"
#include "KlipperLink.h"
#include "Logger.h"

#define SPIFFS LittleFS

extern const char *firmwareVersion;
extern const char *chipFamily;

WebServer::WebServer(int port) : server(port) {}

void WebServer::begin()
{
    // --- Settings ---

    server.on("/get_settings", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  request->send(200, "application/json",
                                settingsManager.toJson(false));
              });

    // Only keys present in the request are changed, so partial updates are safe.
    server.addHandler(new AsyncCallbackJsonWebHandler(
        "/update_settings",
        [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject obj = json.as<JsonObject>();
            if (obj.containsKey("ssid"))
                settingsManager.setSSID(obj["ssid"].as<String>());
            if (obj.containsKey("elegooip"))
                settingsManager.setElegooIP(obj["elegooip"].as<String>());
            if (obj.containsKey("passwd") && obj["passwd"].as<String>().length() > 0)
                settingsManager.setPassword(obj["passwd"].as<String>());
            if (obj.containsKey("ap_mode"))
                settingsManager.setAPMode(obj["ap_mode"].as<bool>());
            if (obj.containsKey("activation_temp"))
                settingsManager.setActivationTemp(obj["activation_temp"].as<float>());
            if (obj.containsKey("control_source"))
                settingsManager.setControlSource(
                    obj["control_source"].as<String>() == "chamber" ? "chamber" : "heater");
            if (obj.containsKey("require_printing"))
                settingsManager.setRequirePrinting(obj["require_printing"].as<bool>());
            if (obj.containsKey("target_temp"))
                settingsManager.setTargetTemp(obj["target_temp"].as<float>());
            if (obj.containsKey("hysteresis"))
                settingsManager.setHysteresis(obj["hysteresis"].as<float>());
            if (obj.containsKey("enabled"))
                settingsManager.setEnabled(obj["enabled"].as<bool>());
            if (obj.containsKey("debug"))
                settingsManager.setDebug(obj["debug"].as<bool>());
            if (obj.containsKey("max_chamber_temp"))
                settingsManager.setMaxChamberTemp(obj["max_chamber_temp"].as<float>());

            bool profileChanged = false;
            if (obj.containsKey("printer_profile"))
            {
                String profile = obj["printer_profile"].as<String>();
                profileChanged = profile != settingsManager.getPrinterProfile() &&
                                 (profile == PROFILE_ELEGOO || profile == PROFILE_KLIPPER);
                if (profileChanged)
                    settingsManager.setPrinterProfile(profile);
            }

            settingsManager.save();
            if (profileChanged)
                settingsManager.requestRestart = true;
            request->send(200, "text/plain", profileChanged ? "restarting" : "ok");
        }));

    // --- Klipper plugin link (printer profile "klipper") ---
    // The cc_heater Klipper plugin POSTs here once per second:
    //   {"target": 45.0, "chamber_temp": 31.2, "klippy": "ready", "seq": 123}
    // and gets the heater state back.

    server.addHandler(new AsyncCallbackJsonWebHandler(
        "/api/klipper/sync",
        [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject obj         = json.as<JsonObject>();
            bool       klipperMode = settingsManager.isKlipperProfile();

            float target  = obj["target"].is<float>() ? obj["target"].as<float>() : 0.0f;
            float chamber = obj["chamber_temp"].is<float>() ? obj["chamber_temp"].as<float>() : NAN;
            bool  ready   = obj["klippy"].as<String>() == "ready";
            if (klipperMode)
                klipperLink.handleSync(target, chamber, ready);

            float maxTemp     = settingsManager.getMaxChamberTemp();
            bool  useChamber  = settingsManager.getControlSource() == "chamber";
            float controlTemp = klipperLink.getControlTemp();

            DynamicJsonDocument doc(512);
            doc["ok"]       = klipperMode;
            doc["profile"]  = settingsManager.getPrinterProfile();
            doc["enabled"]  = settingsManager.getEnabled();
            doc["source"]   = useChamber ? "chamber" : "heater";
            doc["target"]   = min(max(target, 0.0f), maxTemp);
            doc["max_temp"] = maxTemp;
            doc["heating"]  = heaterController.isHeaterActive();
            doc["fan"]      = heaterController.isFanActive();
            doc["fault"]    = klipperMode ? klipperLink.getFault() : "profile";
            doc["fw"]       = firmwareVersion;
            if (klipperMode && !isnan(controlTemp))
                doc["temp"] = controlTemp;
            else
                doc["temp"] = nullptr;
            if (heaterController.getSensorState() == SENSOR_OK)
                doc["heater_temp"] = heaterController.getHeaterTemp();
            else
                doc["heater_temp"] = nullptr;
            if (!isnan(chamber))
                doc["chamber_temp"] = chamber;
            else
                doc["chamber_temp"] = nullptr;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        }));

    // --- Debug override ---

    server.addHandler(new AsyncCallbackJsonWebHandler(
        "/debug_override",
        [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject obj                  = json.as<JsonObject>();
            settingsManager.debugIsPrinting  = obj["isPrinting"].as<bool>();
            settingsManager.debugBedTemp     = obj["bedTemp"].as<float>();
            settingsManager.debugHeaterTemp  = obj["heaterTemp"].as<float>();
            settingsManager.debugChamberTemp = obj["chamberTemp"].as<float>();
            request->send(200, "text/plain", "ok");
        }));

    // --- Status ---

    server.on("/api/status", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  printer_info_t printer = elegooCC.getCurrentInformation();

                  bool debugMode = settingsManager.getDebug();

                  bool           klipperMode = settingsManager.isKlipperProfile();
                  klipper_info_t klipper     = klipperLink.getInfo();

                  DynamicJsonDocument doc(1024);
                  doc["profile"]        = settingsManager.getPrinterProfile();
                  doc["controlSource"]  = settingsManager.getControlSource();
                  doc["maxChamberTemp"] = settingsManager.getMaxChamberTemp();
                  doc["heaterActive"]   = heaterController.isHeaterActive();
                  doc["fanActive"]      = heaterController.isFanActive();
                  doc["heaterTemp"]     = heaterController.getHeaterTemp();
                  doc["heaterSensorOk"] = heaterController.getSensorState() == SENSOR_OK;
                  doc["bedTemp"]        = debugMode ? settingsManager.debugBedTemp : printer.bedTemp;
                  if (klipperMode)
                      doc["chamberTemp"] = isnan(klipper.chamberTemp) ? 0.0f : klipper.chamberTemp;
                  else
                      doc["chamberTemp"] = printer.chamberTemp;
                  doc["debug"] = debugMode;
                  doc["rssi"]  = WiFi.RSSI();

                  JsonObject elegoo     = doc.createNestedObject("elegoo");
                  elegoo["isConnected"] = printer.isWebsocketConnected;
                  elegoo["isPrinting"]  = debugMode ? settingsManager.debugIsPrinting : printer.isPrinting;
                  elegoo["printStatus"] = (int) printer.printStatus;

                  JsonObject k       = doc.createNestedObject("klipper");
                  k["linked"]        = klipper.linked;
                  k["everLinked"]    = klipper.everLinked;
                  k["target"]        = klipper.target;
                  k["klippyReady"]   = klipper.klippyReady;
                  k["lastSyncAgoMs"] = klipper.lastSyncAgoMs;
                  k["syncCount"]     = klipper.syncCount;
                  k["fault"]         = klipperLink.getFault();
                  float controlTemp  = klipperLink.getControlTemp();
                  if (isnan(controlTemp))
                      k["controlTemp"] = nullptr;
                  else
                      k["controlTemp"] = controlTemp;
                  if (isnan(klipper.chamberTemp))
                      k["chamberTemp"] = nullptr;
                  else
                      k["chamberTemp"] = klipper.chamberTemp;

                  String response;
                  serializeJson(doc, response);
                  request->send(200, "application/json", response);
              });

    // --- Logs ---

    server.on("/api/logs", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  request->send(200, "application/json", logger.getLogsAsJson());
              });

    // --- Version ---

    server.on("/version", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  DynamicJsonDocument doc(256);
                  doc["firmware_version"] = firmwareVersion;
                  doc["chip_family"]      = chipFamily;
                  doc["build_date"]       = __DATE__;
                  doc["build_time"]       = __TIME__;
                  String response;
                  serializeJson(doc, response);
                  request->send(200, "application/json", response);
              });

    // --- Restart ---

    server.on("/restart", HTTP_POST,
              [](AsyncWebServerRequest *request)
              {
                  request->send(200, "text/plain", "Restarting...");
                  delay(100);
                  ESP.restart();
              });

    // --- OTA ---
    ElegantOTA.begin(&server);

    // --- Static files ---
    server.serveStatic("/assets/", SPIFFS, "/assets/");
    server.serveStatic("/", SPIFFS, "/");

    // SPA fallback: serve index.htm for any unmatched route so the client-side
    // router can handle paths like /about, /settings, etc. on hard refresh.
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/index.htm", "text/html");
    });

    server.begin();
}

void WebServer::loop()
{
    ElegantOTA.loop();
}
