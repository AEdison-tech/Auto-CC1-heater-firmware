#include "ElegooCC.h"

#include <ArduinoJson.h>

#include "Logger.h"
#include "SettingsManager.h"

extern unsigned long getTime();

ElegooCC &ElegooCC::getInstance()
{
    static ElegooCC instance;
    return instance;
}

ElegooCC::ElegooCC()
{
    printStatus       = SDCP_PRINT_STATUS_IDLE;
    machineStatusMask = 0;
    bedTemp           = 0.0f;
    chamberTemp       = 0.0f;
    lastPing          = 0;

    webSocket.onEvent([this](WStype_t type, uint8_t *payload, size_t length)
                      { this->webSocketEvent(type, payload, length); });
}

void ElegooCC::setup()
{
    if (!settingsManager.isAPMode())
    {
        connect();
    }
}

void ElegooCC::webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
        case WStype_DISCONNECTED:
            logger.log("Disconnected from Carbon Centauri");
            break;
        case WStype_CONNECTED:
            logger.log("Connected to Carbon Centauri");
            sendStatusRequest();
            break;
        case WStype_TEXT:
        {
            StaticJsonDocument<2048> doc;
            DeserializationError     error = deserializeJson(doc, payload);
            if (error)
            {
                logger.logf("JSON parsing failed: %s", error.c_str());
                return;
            }
            if (doc.containsKey("Status"))
            {
                handleStatus(doc);
            }
        }
        break;
        case WStype_BIN:
            logger.log("Received unsupported binary data");
            break;
        case WStype_ERROR:
            logger.logf("WebSocket error: %s", payload);
            break;
        default:
            break;
    }
}

void ElegooCC::handleStatus(JsonDocument &doc)
{
    JsonObject status = doc["Status"];

    if (status.containsKey("CurrentStatus"))
    {
        JsonArray currentStatus = status["CurrentStatus"];
        int       statuses[5];
        int       count = min((int) currentStatus.size(), 5);
        for (int i = 0; i < count; i++)
        {
            statuses[i] = currentStatus[i].as<int>();
        }
        setMachineStatuses(statuses, count);
    }

    if (status.containsKey("TempOfHotbed"))
    {
        bedTemp = status["TempOfHotbed"].as<float>();
    }

    if (status.containsKey("TempOfBox"))
    {
        chamberTemp = status["TempOfBox"].as<float>();
    }

    if (status.containsKey("PrintInfo"))
    {
        JsonObject          printInfo = status["PrintInfo"];
        sdcp_print_status_t newStatus = printInfo["Status"].as<sdcp_print_status_t>();
        if (newStatus != printStatus && newStatus == SDCP_PRINT_STATUS_PRINTING)
        {
            logger.log("Print status changed to printing");
        }
        printStatus = newStatus;
    }
}

void ElegooCC::sendStatusRequest()
{
    if (!webSocket.isConnected())
        return;

    uuid.generate();
    String uuidStr = String(uuid.toCharArray());
    uuidStr.replace("-", "");

    String payload = "{\"Id\":\"" + uuidStr + "\",\"Data\":{\"Cmd\":0,\"Data\":{},"
                     "\"RequestID\":\"" + uuidStr + "\",\"MainboardID\":\"\","
                     "\"TimeStamp\":" + String(getTime()) + ",\"From\":2}}";

    webSocket.sendTXT(payload);
}

void ElegooCC::connect()
{
    if (webSocket.isConnected())
    {
        webSocket.disconnect();
    }
    webSocket.setReconnectInterval(3000);
    ipAddress = settingsManager.getElegooIP();
    logger.logf("Connecting to Elegoo CC @ %s", ipAddress.c_str());
    webSocket.begin(ipAddress, CARBON_CENTAURI_PORT, "/websocket");
}

void ElegooCC::loop()
{
    unsigned long currentTime = millis();

    if (ipAddress != settingsManager.getElegooIP())
    {
        connect();
    }

    if (webSocket.isConnected() && currentTime - lastPing > 29900)
    {
        // sendPing() doesn't work reliably; raw text does
        webSocket.sendTXT("ping");
        lastPing = currentTime;
    }

    webSocket.loop();
}

bool ElegooCC::hasMachineStatus(sdcp_machine_status_t status)
{
    return (machineStatusMask & (1 << status)) != 0;
}

void ElegooCC::setMachineStatuses(const int *statusArray, int arraySize)
{
    machineStatusMask = 0;
    for (int i = 0; i < arraySize; i++)
    {
        if (statusArray[i] >= 0 && statusArray[i] <= 4)
        {
            machineStatusMask |= (1 << statusArray[i]);
        }
    }
}

bool ElegooCC::isPrinting()
{
    return printStatus == SDCP_PRINT_STATUS_PRINTING &&
           hasMachineStatus(SDCP_MACHINE_STATUS_PRINTING);
}

printer_info_t ElegooCC::getCurrentInformation()
{
    printer_info_t info;
    info.printStatus          = printStatus;
    info.isPrinting           = isPrinting();
    info.isWebsocketConnected = webSocket.isConnected();
    info.bedTemp              = bedTemp;
    info.chamberTemp          = chamberTemp;
    return info;
}
