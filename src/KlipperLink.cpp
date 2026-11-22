#include "KlipperLink.h"

#include <cmath>

KlipperLink &KlipperLink::getInstance()
{
    static KlipperLink instance;
    return instance;
}

KlipperLink::KlipperLink()
    : mux(portMUX_INITIALIZER_UNLOCKED),
      target(0.0f),
      chamberTemp(NAN),
      klippyReady(false),
      everLinked(false),
      lastSyncMs(0),
      syncCount(0),
      controlTemp(NAN),
      fault("")
{
}

void KlipperLink::handleSync(float newTarget, float newChamberTemp, bool newKlippyReady)
{
    if (isnan(newTarget) || newTarget < 0.0f)
        newTarget = 0.0f;

    portENTER_CRITICAL(&mux);
    target      = newTarget;
    chamberTemp = newChamberTemp;
    klippyReady = newKlippyReady;
    everLinked  = true;
    lastSyncMs  = millis();
    syncCount++;
    portEXIT_CRITICAL(&mux);
}

klipper_info_t KlipperLink::getInfo()
{
    klipper_info_t info;
    unsigned long  now = millis();

    portENTER_CRITICAL(&mux);
    info.target      = target;
    info.chamberTemp = chamberTemp;
    info.klippyReady = klippyReady;
    info.everLinked  = everLinked;
    info.syncCount   = syncCount;
    unsigned long last = lastSyncMs;
    portEXIT_CRITICAL(&mux);

    info.lastSyncAgoMs = info.everLinked ? now - last : 0;
    info.linked        = info.everLinked && info.lastSyncAgoMs <= KLIPPER_LINK_TIMEOUT_MS;
    return info;
}

void KlipperLink::publish(float newControlTemp, const char *newFault)
{
    controlTemp = newControlTemp;
    fault       = newFault ? newFault : "";
}

float KlipperLink::getControlTemp() const
{
    return controlTemp;
}

const char *KlipperLink::getFault() const
{
    return fault;
}
