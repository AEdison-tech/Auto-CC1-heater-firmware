# Kalico/Klipper plugin for the Auto-CC1 chamber heater (ESP32-C3 add-on)
#
# Exposes the heater as a regular Klipper heater ([heater_generic]), so it
# shows up in Fluidd / Mainsail / GrumpyScreen with a live temperature and a
# settable target, and can be driven with M141, M191 (via CHAMBER_HEATER_WAIT),
# SET_HEATER_TEMPERATURE and TURN_OFF_HEATERS.
#
# Division of labour:
#   * Klipper owns the *target* (slicer G-code, UI, TURN_OFF_HEATERS, ...).
#   * The ESP32 owns the *regulation*: hysteresis on its selected sensor
#     (its own DS18B20 or the printer chamber sensor this module forwards).
#   * Once per second this module POSTs the target to the heater and feeds the
#     temperature it reports back into Klipper. The ESP32 switches itself off
#     when it has not heard from Klipper for 10 s (that watchdog is the real
#     fail-safe for a crashed or hung host).
#
# All network I/O runs on a background thread; the Klipper reactor never
# blocks on the heater.
#
# A "*.local" host name is resolved by this module with a one-shot mDNS query
# (hosts like COSMOS have no system mDNS resolver) and looked up again when
# the heater stops answering, e.g. after it got a new address from DHCP.
#
# Example configuration (order matters: [cc_heater] before the heater):
#
#   [cc_heater]
#   host: ccheater.local
#
#   [heater_generic chamber_heater]
#   heater_pin: cc_heater:pwm
#   sensor_type: cc_heater
#   control: watermark
#   max_delta: 1.0
#   min_temp: -10
#   max_temp: 80
#
# Copyright (C) 2026  AEdison-tech
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import http.client
import json
import logging
import random
import socket
import struct
import threading
import time

API_PATH = "/api/klipper/sync"
REPORT_TIME = 1.0
PROMPT_MACRO = "gcode_macro _SHOW_PROMPT"

MDNS_GROUP = ("224.0.0.251", 5353)
MDNS_RETRY_FAILURES = 2   # failed requests before a .local name is looked up again
MDNS_RETRY_INTERVAL = 5.  # seconds between mDNS lookups while it does not answer,
MDNS_RETRY_MAX = 30.      # doubling up to this; a new target retries immediately

# Device-reported faults that stop the heater and trigger fault_action.
DEVICE_FAULTS = {
    "sensor": "the heater's control sensor has no valid reading",
    "overtemp": "the heater reports over-temperature",
    "profile": "the heater is not in Klipper mode (set its printer profile "
               "to Klipper / COSMOS)",
}
# Device-reported states that only produce a warning.
DEVICE_WARNINGS = {
    "disabled": "heater control is disabled in the heater's web UI",
}

logger = logging.getLogger("cc_heater")


class CCHeater:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.reactor = self.printer.get_reactor()
        self.gcode = self.printer.lookup_object("gcode")

        self.host = config.get("host").strip()
        self._mdns_name = None
        if self.host.lower().rstrip(".").endswith(".local"):
            self._mdns_name = self.host.lower().rstrip(".")
            try:
                _dns_query(self._mdns_name, 0)
            except ValueError as e:
                raise config.error("cc_heater: %s" % (e,))
        self.port = config.getint("port", 80, minval=1, maxval=65535)
        self.heater_name = config.get("heater", "chamber_heater")
        self.chamber_sensor_name = config.get(
            "chamber_sensor", "temperature_sensor chamber")
        self.max_target = config.getfloat("max_target", 65., above=0.)
        self.sync_interval = config.getfloat(
            "sync_interval", 1., minval=.25, maxval=5.)
        self.link_timeout = config.getfloat("link_timeout", 10., minval=3.)
        self.request_timeout = config.getfloat(
            "request_timeout", 2., above=0., maxval=10.)
        self.wait_timeout = config.getfloat("wait_timeout", 3600., minval=0.)
        self.fault_action = config.getchoice(
            "fault_action",
            {"pause": "pause", "warn": "warn", "shutdown": "shutdown"},
            "pause")

        # State shared with the network thread (guarded by _lock)
        self._lock = threading.Lock()
        self._wake = threading.Event()
        self._stop = threading.Event()
        self._sent = threading.Event()
        self._request = {"target": 0., "chamber_temp": None,
                         "klippy": "startup"}
        self._reply = None
        self._reply_count = 0
        self._last_error = None
        self._address = None if self._mdns_name else self.host
        self._thread = None
        self._resolve_time = None  # network thread only
        self._resolve_interval = MDNS_RETRY_INTERVAL

        # Reactor-side state
        self._heater = None
        self._orig_set_temp = None
        self._chamber_sensor = None
        self._sensor = None
        self._pin = None
        self._mcu = None
        self._ready = False
        self._seen_reply_count = 0
        self._last_ok_time = None
        self._start_time = 0.
        self._was_linked = None
        self._label = self.host
        self._fresh_time = max(3., 2. * self.sync_interval
                               + self.request_timeout)
        self._target = 0.
        self._target_time = 0.
        self._fault = ""
        self._fault_latched = False
        self._warned = set()
        self._status = self._build_status(None, False, None)

        pheaters = self.printer.load_object(config, "heaters")
        pheaters.add_sensor_factory("cc_heater", self._create_sensor)
        self.printer.lookup_object("pins").register_chip("cc_heater", self)

        self.printer.register_event_handler(
            "klippy:connect", self._handle_connect)
        self.printer.register_event_handler(
            "klippy:ready", self._handle_ready)
        self.printer.register_event_handler(
            "klippy:shutdown", self._handle_shutdown)
        self.printer.register_event_handler(
            "klippy:disconnect", self._handle_disconnect)

        self.gcode.register_command(
            "M141", self.cmd_M141, desc=self.cmd_M141_help)
        self.gcode.register_command(
            "CHAMBER_HEATER_WAIT", self.cmd_CHAMBER_HEATER_WAIT,
            desc=self.cmd_CHAMBER_HEATER_WAIT_help)
        self.gcode.register_command(
            "CC_HEATER_STATUS", self.cmd_CC_HEATER_STATUS,
            desc=self.cmd_CC_HEATER_STATUS_help)
        # COSMOS already defines M191 as a gcode_macro; only register our own
        # when asked to (stock Klipper/Kalico without such a macro).
        if config.getboolean("register_m191", False):
            self.gcode.register_command(
                "M191", self.cmd_M191, desc=self.cmd_M191_help)

        self._timer = self.reactor.register_timer(self._update_event)

    # ------------------------------------------------------------------
    # Klipper glue: sensor factory and virtual pin
    # ------------------------------------------------------------------

    def _create_sensor(self, config):
        if self._sensor is not None:
            raise config.error("cc_heater: only one heater may use "
                               "sensor_type: cc_heater")
        self._sensor = CCHeaterSensor()
        return self._sensor

    def setup_pin(self, pin_type, pin_params):
        if pin_type != "pwm" or pin_params["pin"] != "pwm":
            raise self.printer.config_error(
                "cc_heater: only 'cc_heater:pwm' is supported as heater_pin")
        if self._pin is not None:
            raise self.printer.config_error(
                "cc_heater: 'cc_heater:pwm' may only be used once")
        self._pin = CCHeaterPin(self.printer)
        return self._pin

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def _handle_connect(self):
        pheaters = self.printer.lookup_object("heaters")
        heater = pheaters.lookup_heater(self.heater_name)
        if (self._pin is None or self._sensor is None
                or getattr(heater, "mcu_pwm", None) is not self._pin):
            raise self.printer.config_error(
                "cc_heater: [heater_generic %s] must use 'heater_pin: "
                "cc_heater:pwm' and 'sensor_type: cc_heater'"
                % (self.heater_name,))
        self._heater = heater
        self._mcu = self.printer.lookup_object("mcu")
        self._chamber_sensor = self.printer.lookup_object(
            self.chamber_sensor_name, None)
        if self._chamber_sensor is None:
            logger.warning("cc_heater: chamber sensor '%s' not found",
                           self.chamber_sensor_name)
        self._hook_heater(heater)
        self._hook_verify_heater()
        self._start_time = self.reactor.monotonic()
        self._thread = threading.Thread(
            target=self._run, name="cc_heater", daemon=True)
        self._thread.start()
        self.reactor.update_timer(self._timer, self.reactor.NOW)
        logger.info("cc_heater: controlling heater at %s:%d%s",
                    self.host, self.port,
                    " (resolved via mDNS)" if self._mdns_name else "")

    def _handle_ready(self):
        self._ready = True

    def _handle_shutdown(self):
        self._target = 0.
        with self._lock:
            self._request["target"] = 0.
            self._request["klippy"] = "shutdown"
        self._wake.set()

    def _handle_disconnect(self):
        # Klipper restart or exit: make one last attempt to switch the heater
        # off (its watchdog would do so anyway after link_timeout).
        self._target = 0.
        with self._lock:
            self._request["target"] = 0.
            self._request["klippy"] = "disconnect"
        self.reactor.update_timer(self._timer, self.reactor.NEVER)
        if self._thread is not None and self._thread.is_alive():
            self._sent.clear()
            self._wake.set()
            self._sent.wait(self.request_timeout + .5)
        self._stop.set()
        self._wake.set()

    def _hook_heater(self, heater):
        if self._orig_set_temp is not None:
            return
        self._orig_set_temp = orig = heater.set_temp

        def set_temp(degrees):
            if degrees > self.max_target:
                self.gcode.respond_info(
                    "Chamber heater: target %.1f C clamped to %.1f C"
                    % (degrees, self.max_target))
                degrees = self.max_target
            orig(degrees)
            self._on_target(degrees)

        heater.set_temp = set_temp

    def _hook_verify_heater(self):
        # verify_heater would shut Klipper down when the chamber does not heat
        # as expected. Route that through fault_action instead (default: turn
        # the heater off and pause the print).
        if self.fault_action == "shutdown":
            return
        check = self.printer.lookup_object(
            "verify_heater %s" % (self.heater_name,), None)
        if check is None:
            return

        def heater_fault():
            check.error = 0.
            check.approaching_target = check.starting_approach = False
            now = self.reactor.monotonic()
            self._raise_fault(
                now, "the chamber is not heating at the expected rate")
            return now + 1.

        check.heater_fault = heater_fault

    # ------------------------------------------------------------------
    # Target handling
    # ------------------------------------------------------------------

    def _on_target(self, degrees):
        if degrees == self._target:
            return
        self._target = degrees
        self._target_time = self.reactor.monotonic()
        if degrees > 0.:
            self._fault_latched = False
            self._fault = ""
            self._warned.clear()
        with self._lock:
            self._request["target"] = degrees
        self._wake.set()

    def _force_off(self):
        if self._heater is not None and self._orig_set_temp is not None:
            try:
                self._orig_set_temp(0.)
            except Exception:
                logger.exception("cc_heater: unable to reset heater target")
        self._on_target(0.)

    def _require_heater(self, gcmd):
        if self._heater is None:
            raise gcmd.error("cc_heater: heater '%s' is not ready"
                             % (self.heater_name,))
        return self._heater

    # ------------------------------------------------------------------
    # Periodic update (reactor thread)
    # ------------------------------------------------------------------

    def _update_event(self, eventtime):
        shutdown = self.printer.is_shutdown()
        target = 0.
        if self._heater is not None and not shutdown:
            target = self._heater.get_temp(eventtime)[1]
        if target != self._target:
            self._on_target(target)

        chamber = None
        if self._chamber_sensor is not None:
            try:
                chamber = float(self._chamber_sensor.get_temp(eventtime)[0])
            except Exception:
                chamber = None

        if shutdown:
            klippy = "shutdown"
        elif self._ready:
            klippy = "ready"
        else:
            klippy = "startup"

        with self._lock:
            self._request["chamber_temp"] = chamber
            self._request["klippy"] = klippy
            self._request["target"] = self._target
            reply = self._reply
            count = self._reply_count
            error = self._last_error
            address = self._address
        self._label = self._host_label(address)

        if count != self._seen_reply_count:
            self._seen_reply_count = count
            self._last_ok_time = eventtime
        linked = (self._last_ok_time is not None
                  and eventtime - self._last_ok_time <= self.link_timeout)

        # Temperature shown for the heater: its control temperature while it
        # answers, 0 once it is offline so Fluidd / the display make the outage
        # visible. During a short gap (or without a valid reading) nothing is
        # reported and Klipper keeps the last value.
        fresh = (self._last_ok_time is not None
                 and eventtime - self._last_ok_time <= self._fresh_time)
        temp = None
        if not linked:
            temp = 0.
        elif fresh and reply is not None:
            temp = _as_float(reply.get("temp"))
        if temp is not None and self._sensor is not None:
            self._sensor.report(
                self._mcu.estimated_print_time(eventtime), temp)

        self._report_link_change(eventtime, linked, error)
        self._status = self._build_status(reply, linked, error, address)
        self._check_faults(eventtime, linked, reply, error)
        return eventtime + self.sync_interval

    def _host_label(self, address):
        if address and address != self.host:
            return "%s, %s" % (self.host, address)
        return self.host

    def _report_link_change(self, eventtime, linked, error):
        if linked == self._was_linked:
            return
        if (not linked and self._was_linked is None
                and eventtime - self._start_time <= self.link_timeout):
            return  # still starting up, give the heater a chance to answer
        previous = self._was_linked
        self._was_linked = linked
        if linked:
            if previous is not None:  # stay quiet on a normal startup
                self.gcode.respond_info(
                    "Chamber heater connected (%s)" % (self._label,))
        else:
            self.gcode.respond_info(
                "Chamber heater offline (%s): %s"
                % (self._label, error or "no reply"))

    def _check_faults(self, eventtime, linked, reply, error):
        if self._target <= 0. or self._fault_latched:
            return
        if self.printer.is_shutdown():
            return
        if not linked:
            since = max(self._last_ok_time or 0., self._target_time)
            if eventtime - since > self.link_timeout:
                self._raise_fault(
                    eventtime, "no response from the heater at %s (%s)"
                    % (self._label, error or "timeout"))
            return
        device_fault = (reply or {}).get("fault") or ""
        if device_fault in DEVICE_FAULTS:
            self._raise_fault(eventtime, DEVICE_FAULTS[device_fault])
        elif (device_fault in DEVICE_WARNINGS
              and device_fault not in self._warned):
            self._warned.add(device_fault)
            self.gcode.respond_info(
                "Chamber heater: %s" % (DEVICE_WARNINGS[device_fault],))

    def _raise_fault(self, eventtime, reason):
        if self._fault_latched:
            return
        self._fault_latched = True
        self._fault = reason
        msg = "Chamber heater fault: %s" % (reason,)
        logger.error("cc_heater: %s", msg)
        if self.fault_action == "shutdown":
            self.printer.invoke_shutdown(msg)
            return
        self._force_off()
        pausing = self.fault_action == "pause" and self._is_printing(eventtime)
        self.gcode.respond_raw(
            "!! %s%s" % (msg, " - pausing the print" if pausing else ""))
        self.reactor.register_callback(
            lambda e: self._notify_and_pause(e, reason, pausing))

    def _is_printing(self, eventtime):
        print_stats = self.printer.lookup_object("print_stats", None)
        if print_stats is None:
            return False
        return print_stats.get_status(eventtime).get("state") == "printing"

    def _notify_and_pause(self, eventtime, reason, pausing):
        if pausing:
            pause_resume = self.printer.lookup_object("pause_resume", None)
            if (pause_resume is not None
                    and not pause_resume.get_status(eventtime)["is_paused"]):
                pause_resume.send_pause_command()
                try:
                    self.gcode.run_script("PAUSE")
                except Exception:
                    logger.exception("cc_heater: PAUSE failed")
        if self.printer.lookup_object(PROMPT_MACRO, None) is not None:
            message = "Heater switched off: %s.%s" % (
                reason, " The print was paused." if pausing else "")
            message = message.replace('"', "'")
            try:
                self.gcode.run_script(
                    '_SHOW_PROMPT TITLE="Chamber heater fault" MESSAGE="%s"'
                    % (message,))
            except Exception:
                logger.exception("cc_heater: unable to show prompt")

    def _build_status(self, reply, linked, error, address=None):
        reply = reply if (linked and reply is not None) else {}
        return {
            "connected": bool(linked),
            "host": self.host,
            "address": address or "",
            "heating": bool(reply.get("heating", False)),
            "fan": bool(reply.get("fan", False)),
            "fault": self._fault,
            "device_fault": reply.get("fault") or "",
            "source": reply.get("source") or "",
            "heater_temp": _as_float(reply.get("heater_temp")),
            "chamber_temp": _as_float(reply.get("chamber_temp")),
            "device_target": _as_float(reply.get("target")),
            "max_temp": _as_float(reply.get("max_temp")),
            "firmware": reply.get("fw") or "",
            "last_error": error or "",
        }

    def get_status(self, eventtime):
        return dict(self._status)

    # ------------------------------------------------------------------
    # Network thread
    # ------------------------------------------------------------------

    def _run(self):
        seq = 0
        failing = False
        failures = 0
        last_target = 0.
        while not self._stop.is_set():
            self._wake.clear()
            with self._lock:
                body = dict(self._request)
            seq += 1
            body["seq"] = seq
            new_target = body["target"] > 0. and body["target"] != last_target
            last_target = body["target"]
            try:
                reply = self._post(
                    self._get_address(failures, new_target), body)
                with self._lock:
                    self._reply = reply
                    self._reply_count += 1
                    self._last_error = None
                failures = 0
                if failing:
                    logger.info("cc_heater: link to %s restored", self.host)
                    failing = False
            except Exception as e:
                failures += 1
                with self._lock:
                    self._last_error = str(e) or e.__class__.__name__
                if not failing:
                    logger.warning("cc_heater: request to %s failed: %s",
                                   self.host, e)
                    failing = True
            self._sent.set()
            self._wake.wait(self.sync_interval)

    def _get_address(self, failures, retry_now=False):
        """Address for the next request (network thread). A .local name is
        looked up via mDNS at startup and again once the heater stops
        answering; until a new answer arrives the last address is kept."""
        if self._mdns_name is None:
            return self.host
        address = self._address
        if address is not None and failures < MDNS_RETRY_FAILURES:
            return address
        now = time.monotonic()
        if (retry_now or self._resolve_time is None
                or now - self._resolve_time >= self._resolve_interval):
            self._resolve_time = now
            found = mdns_resolve(self._mdns_name, self.request_timeout)
            if found is None:
                self._resolve_interval = min(2. * self._resolve_interval,
                                             MDNS_RETRY_MAX)
            else:
                self._resolve_interval = MDNS_RETRY_INTERVAL
            if found is not None and found != address:
                logger.info("cc_heater: %s resolved to %s", self.host, found)
                address = found
                with self._lock:
                    self._address = found
        if address is None:
            raise RuntimeError("%s not found (no mDNS answer)" % (self.host,))
        return address

    def _post(self, address, body):
        conn = http.client.HTTPConnection(
            address, self.port, timeout=self.request_timeout)
        try:
            conn.request("POST", API_PATH, json.dumps(body),
                         {"Content-Type": "application/json",
                          "Connection": "close"})
            resp = conn.getresponse()
            data = resp.read()
            if resp.status != 200:
                raise RuntimeError("HTTP %d" % (resp.status,))
            reply = json.loads(data.decode("utf-8"))
            if not isinstance(reply, dict):
                raise ValueError("unexpected reply")
            return reply
        finally:
            conn.close()

    # ------------------------------------------------------------------
    # G-code commands
    # ------------------------------------------------------------------

    cmd_M141_help = "Set the chamber heater target (S0 = off)"

    def cmd_M141(self, gcmd):
        temp = gcmd.get_float("S", 0., minval=0.)
        heater = self._require_heater(gcmd)
        pheaters = self.printer.lookup_object("heaters")
        pheaters.set_temperature(heater, temp)

    cmd_M191_help = "Set the chamber heater target and wait for it"

    def cmd_M191(self, gcmd):
        self._wait(gcmd, gcmd.get_float("S", 0., minval=0.), None,
                   gcmd.get_float("T", self.wait_timeout, minval=0.))

    cmd_CHAMBER_HEATER_WAIT_help = (
        "Wait until the chamber reaches MINIMUM (raises the target to MINIMUM "
        "if it is lower, never lowers it). Optional TARGET, TIMEOUT in s")

    def cmd_CHAMBER_HEATER_WAIT(self, gcmd):
        self._wait(gcmd, gcmd.get_float("MINIMUM", 0., minval=0.),
                   gcmd.get_float("TARGET", None, minval=0.),
                   gcmd.get_float("TIMEOUT", self.wait_timeout, minval=0.))

    def _wait(self, gcmd, minimum, target, timeout):
        heater = self._require_heater(gcmd)
        pheaters = self.printer.lookup_object("heaters")
        now = self.reactor.monotonic()
        if target is not None:
            pheaters.set_temperature(heater, target)
        elif minimum > heater.get_temp(now)[1]:
            pheaters.set_temperature(heater, minimum)
        else:
            # Flush queued moves (e.g. a park move) like M109/M190 do
            toolhead = self.printer.lookup_object("toolhead")
            toolhead.register_lookahead_callback(lambda pt: None)
        current_target = heater.get_temp(now)[1]
        if minimum <= 0. or current_target <= 0.:
            return
        minimum = min(minimum, current_target)
        gcmd.respond_info(
            "Chamber heater: waiting for %.1f C (target %.1f C, timeout %s)"
            % (minimum, current_target,
               "%d min" % (timeout / 60.,) if timeout > 0. else "none"))
        start = self.reactor.monotonic()
        state = {"result": "wait", "last_report": start}

        def check(eventtime):
            temp, tgt = heater.get_temp(eventtime)
            if tgt <= 0.:
                state["result"] = "off"
                return False
            if temp >= minimum:
                state["result"] = "reached"
                return False
            if timeout > 0. and eventtime - start >= timeout:
                state["result"] = "timeout"
                return False
            if eventtime - state["last_report"] >= 30.:
                state["last_report"] = eventtime
                gcmd.respond_info(
                    "Chamber %.1f C, waiting for %.1f C (%d min elapsed)"
                    % (temp, minimum, (eventtime - start) / 60.))
            return True

        self.printer.wait_while(check)
        elapsed = (self.reactor.monotonic() - start) / 60.
        if state["result"] == "reached":
            gcmd.respond_info("Chamber heater: %.1f C reached after %.0f min"
                              % (minimum, elapsed))
        elif state["result"] == "timeout":
            gcmd.respond_raw(
                "!! Chamber heater: %.1f C not reached within %.0f min, "
                "continuing (heating continues to %.1f C)"
                % (minimum, elapsed, heater.get_temp(
                    self.reactor.monotonic())[1]))
        else:
            gcmd.respond_info("Chamber heater: wait aborted, heater is off")

    cmd_CC_HEATER_STATUS_help = "Report the chamber heater link status"

    def cmd_CC_HEATER_STATUS(self, gcmd):
        s = self._status
        where = self.host
        if s["address"] and s["address"] != self.host:
            where = "%s (%s)" % (self.host, s["address"])
        lines = ["Chamber heater @ %s: %s" % (
            where, "connected" if s["connected"] else "NOT connected")]
        if s["connected"]:
            lines.append("source=%s heating=%s fan=%s device_target=%s "
                         "heater_temp=%s chamber_temp=%s fw=%s"
                         % (s["source"], s["heating"], s["fan"],
                            _fmt(s["device_target"]), _fmt(s["heater_temp"]),
                            _fmt(s["chamber_temp"]), s["firmware"]))
        if s["device_fault"]:
            lines.append("device fault: %s" % (s["device_fault"],))
        if s["fault"]:
            lines.append("last fault: %s" % (s["fault"],))
        if s["last_error"]:
            lines.append("last error: %s" % (s["last_error"],))
        gcmd.respond_info("\n".join(lines))


class CCHeaterSensor:
    """Temperature 'sensor' fed by the values the heater reports."""

    def __init__(self):
        self._callback = None
        self.min_temp = self.max_temp = None

    def setup_minmax(self, min_temp, max_temp):
        self.min_temp, self.max_temp = min_temp, max_temp

    def setup_callback(self, temperature_callback):
        self._callback = temperature_callback

    def get_report_time_delta(self):
        return REPORT_TIME

    def report(self, read_time, temp):
        if self._callback is not None:
            self._callback(read_time, temp)


class CCHeaterPin:
    """Virtual PWM pin. Klipper's own control output is only recorded; the
    heater regulates itself around the target."""

    def __init__(self, printer):
        self.printer = printer
        self.last_value = 0.

    def get_mcu(self):
        return self.printer.lookup_object("mcu")

    def setup_cycle_time(self, cycle_time, hardware_pwm=False):
        pass

    def setup_max_duration(self, max_duration):
        pass

    def setup_start_value(self, start_value, shutdown_value=0.):
        pass

    def set_pwm(self, print_time, value, cycle_time=None):
        self.last_value = value

    def set_digital(self, print_time, value):
        self.last_value = 1. if value else 0.


def _as_float(value):
    if isinstance(value, bool) or value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _fmt(value):
    return "-" if value is None else "%.1f" % (value,)


# ----------------------------------------------------------------------
# Minimal mDNS client
# ----------------------------------------------------------------------

def mdns_resolve(name, timeout):
    """Resolve a .local host name to an IPv4 address, or return None.

    Sends a one-shot multicast DNS query from an ephemeral port (RFC 6762,
    section 5.1); the responder answers by unicast to that port, so neither
    port 5353 nor a system mDNS service is needed. The query is repeated
    three times within the timeout.
    """
    name = name.rstrip(".").lower()
    packet = _dns_query(name, random.randint(1, 0xFFFF))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
        sock.bind(("", 0))
        deadline = time.monotonic() + timeout
        resend = 0.
        while True:
            now = time.monotonic()
            if now >= deadline:
                return None
            if now >= resend:
                sock.sendto(packet, MDNS_GROUP)
                resend = now + max(timeout / 3., .2)
            sock.settimeout(max(min(deadline, resend) - now, .01))
            try:
                data = sock.recvfrom(4096)[0]
            except socket.timeout:
                continue
            try:
                address = _dns_find_a(data, name)
            except (IndexError, ValueError, struct.error):
                continue  # malformed packet
            if address is not None:
                return address
    finally:
        sock.close()


def _dns_query(name, qid):
    qname = b""
    for label in name.split("."):
        try:
            raw = label.encode("ascii")
        except UnicodeError:
            raise ValueError("invalid host name '%s'" % (name,))
        if not 0 < len(raw) < 64:
            raise ValueError("invalid host name '%s'" % (name,))
        qname += struct.pack("B", len(raw)) + raw
    # Standard query, one question: <name> type A, class IN
    return (struct.pack("!6H", qid, 0, 1, 0, 0, 0) + qname + b"\0"
            + struct.pack("!HH", 1, 1))


def _dns_read_name(data, offset):
    labels = []
    end = None
    for _ in range(128):
        length = data[offset]
        if length & 0xC0 == 0xC0:  # compression pointer
            if end is None:
                end = offset + 2
            offset = ((length & 0x3F) << 8) | data[offset + 1]
            continue
        offset += 1
        if length == 0:
            return ".".join(labels), (offset if end is None else end)
        labels.append(data[offset:offset + length].decode(
            "ascii", "replace").lower())
        offset += length
    raise ValueError("name too long")


def _dns_find_a(data, name):
    flags, qdcount, ancount, nscount, arcount = struct.unpack_from(
        "!5H", data, 2)
    if not flags & 0x8000:
        return None  # a query, not a response
    offset = 12
    for _ in range(qdcount):
        offset = _dns_read_name(data, offset)[1] + 4
    for _ in range(ancount + nscount + arcount):
        rname, offset = _dns_read_name(data, offset)
        rtype, _rclass, _ttl, rdlength = struct.unpack_from(
            "!HHIH", data, offset)
        offset += 10
        if rtype == 1 and rdlength == 4 and rname == name:
            return socket.inet_ntoa(data[offset:offset + 4])
        offset += rdlength
    return None


def load_config(config):
    return CCHeater(config)
