#ifndef KLIPPER_LINK_H
#define KLIPPER_LINK_H

#include <Arduino.h>

// Link to the cc_heater Klipper/Kalico plugin (printer profile "klipper").
//
// The plugin POSTs /api/klipper/sync once per second with the heater target
// set in Klipper and the printer's chamber temperature. When no sync arrives
// for KLIPPER_LINK_TIMEOUT_MS the link counts as lost and the heater turns off.

#define KLIPPER_LINK_TIMEOUT_MS 10000UL

typedef struct
{
    bool          linked;          // a sync arrived within KLIPPER_LINK_TIMEOUT_MS
    bool          everLinked;      // at least one sync since boot
    float         target;          // target requested by Klipper (°C, 0 = off)
    float         chamberTemp;     // printer chamber sensor (NAN if unknown)
    bool          klippyReady;     // Klipper reported state "ready"
    unsigned long lastSyncAgoMs;   // ms since the last sync (0 if never)
    uint32_t      syncCount;
} klipper_info_t;

class KlipperLink
{
   private:
    portMUX_TYPE  mux;
    float         target;
    float         chamberTemp;
    bool          klippyReady;
    bool          everLinked;
    unsigned long lastSyncMs;
    uint32_t      syncCount;

    // Written by the main loop, read by the web handler
    volatile float       controlTemp;
    const char *volatile fault;

    KlipperLink();
    KlipperLink(const KlipperLink &)            = delete;
    KlipperLink &operator=(const KlipperLink &) = delete;

   public:
    static KlipperLink &getInstance();

    // Called from the async web server task.
    void handleSync(float target, float chamberTemp, bool klippyReady);

    klipper_info_t getInfo();

    // Control state published by the main loop for the sync reply.
    void        publish(float controlTemp, const char *fault);
    float       getControlTemp() const;
    const char *getFault() const;
};

#define klipperLink KlipperLink::getInstance()

#endif  // KLIPPER_LINK_H
