#ifndef ELEGOOCC_H
#define ELEGOOCC_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include "UUID.h"

#define CARBON_CENTAURI_PORT 3030

typedef enum
{
    SDCP_PRINT_STATUS_IDLE          = 0,
    SDCP_PRINT_STATUS_HOMING        = 1,
    SDCP_PRINT_STATUS_DROPPING      = 2,
    SDCP_PRINT_STATUS_EXPOSURING    = 3,
    SDCP_PRINT_STATUS_LIFTING       = 4,
    SDCP_PRINT_STATUS_PAUSING       = 5,
    SDCP_PRINT_STATUS_PAUSED        = 6,
    SDCP_PRINT_STATUS_STOPPING      = 7,
    SDCP_PRINT_STATUS_STOPED        = 8,
    SDCP_PRINT_STATUS_COMPLETE      = 9,
    SDCP_PRINT_STATUS_FILE_CHECKING = 10,
    SDCP_PRINT_STATUS_PRINTING      = 13,
    SDCP_PRINT_STATUS_UNKNOWN_15    = 15,
    SDCP_PRINT_STATUS_HEATING       = 16,
    SDCP_PRINT_STATUS_UNKNOWN_18    = 18,
    SDCP_PRINT_STATUS_UNKNOWN_19    = 19,
    SDCP_PRINT_STATUS_BED_LEVELING  = 20,
    SDCP_PRINT_STATUS_UNKNOWN_21    = 21,
} sdcp_print_status_t;

typedef enum
{
    SDCP_MACHINE_STATUS_IDLE              = 0,
    SDCP_MACHINE_STATUS_PRINTING          = 1,
    SDCP_MACHINE_STATUS_FILE_TRANSFERRING = 2,
    SDCP_MACHINE_STATUS_EXPOSURE_TESTING  = 3,
    SDCP_MACHINE_STATUS_DEVICES_TESTING   = 4,
} sdcp_machine_status_t;

typedef struct
{
    sdcp_print_status_t printStatus;
    bool                isWebsocketConnected;
    bool                isPrinting;
    float               bedTemp;     // TempOfHotbed from SDCP status
    float               chamberTemp; // TempOfBox from SDCP status
} printer_info_t;

class ElegooCC
{
   private:
    WebSocketsClient webSocket;
    UUID             uuid;

    String        ipAddress;
    unsigned long lastPing;

    sdcp_print_status_t printStatus;
    uint8_t             machineStatusMask;
    float               bedTemp;
    float               chamberTemp;

    ElegooCC();
    ElegooCC(const ElegooCC &)            = delete;
    ElegooCC &operator=(const ElegooCC &) = delete;

    void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
    void connect();
    void handleStatus(JsonDocument &doc);
    void sendStatusRequest();

    bool hasMachineStatus(sdcp_machine_status_t status);
    void setMachineStatuses(const int *statusArray, int arraySize);
    bool isPrinting();

   public:
    static ElegooCC &getInstance();

    void setup();
    void loop();

    printer_info_t getCurrentInformation();
};

#define elegooCC ElegooCC::getInstance()

#endif  // ELEGOOCC_H
