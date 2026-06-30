#include "WebServer.h"

#include <AsyncJson.h>
#include <WiFi.h>

#include "ElegooCC.h"
#include "HeaterController.h"
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

    server.addHandler(new AsyncCallbackJsonWebHandler(
        "/update_settings",
        [](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject obj = json.as<JsonObject>();
            settingsManager.setSSID(obj["ssid"].as<String>());
            settingsManager.setElegooIP(obj["elegooip"].as<String>());
            if (obj.containsKey("passwd") && obj["passwd"].as<String>().length() > 0)
            {
                settingsManager.setPassword(obj["passwd"].as<String>());
            }
            settingsManager.setAPMode(obj["ap_mode"].as<bool>());
            settingsManager.setActivationTemp(obj["activation_temp"].as<float>());
            settingsManager.setControlSource(obj["control_source"].as<String>());
            settingsManager.setRequirePrinting(obj["require_printing"].as<bool>());
            settingsManager.setTargetTemp(obj["target_temp"].as<float>());
            settingsManager.setHysteresis(obj["hysteresis"].as<float>());
            settingsManager.setEnabled(obj["enabled"].as<bool>());
            settingsManager.setDebug(obj["debug"].as<bool>());
            settingsManager.save();
            request->send(200, "text/plain", "ok");
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

                  DynamicJsonDocument doc(512);
                  doc["heaterActive"] = heaterController.isHeaterActive();
                  doc["fanActive"]    = heaterController.isFanActive();
                  doc["heaterTemp"]   = heaterController.getHeaterTemp();
                  doc["bedTemp"]      = debugMode ? settingsManager.debugBedTemp    : printer.bedTemp;
                  doc["chamberTemp"]  = printer.chamberTemp;
                  doc["debug"]        = debugMode;
                  doc["rssi"]         = WiFi.RSSI();

                  JsonObject elegoo         = doc.createNestedObject("elegoo");
                  elegoo["isConnected"]     = printer.isWebsocketConnected;
                  elegoo["isPrinting"]      = debugMode ? settingsManager.debugIsPrinting : printer.isPrinting;
                  elegoo["printStatus"]     = (int) printer.printStatus;

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
