# Klipper / COSMOS integration (`cc_heater` plugin)

With the **Klipper / COSMOS** printer profile the heater becomes a regular
Klipper heater, as if the printer had a built-in chamber heater:

- `chamber_heater` card with live temperature and a settable target in
  **Fluidd / Mainsail**, controllable on the **GrumpyScreen** display
- `M141 S<temp>` sets the target, `M191 S<temp>` waits for the chamber
- `TURN_OFF_HEATERS` (cooldown button, `PRINT_END`, `CANCEL_PRINT`, idle
  timeout) switches it off
- OrcaSlicer's chamber **Target** and **Minimal** temperatures: the print
  starts once *Minimal* is reached, the heater keeps going to *Target*

Requires heater firmware **1.1.0-beta.1** or newer. Tested on the Elegoo
Centauri Carbon with
[OpenCentauri COSMOS](https://docs.opencentauri.cc/klipper-conversion/cosmos/cosmos/)
26.10.1 (Kalico). It should work on any Kalico/Klipper host that can load
plugins or extras.

## How it works

```
OrcaSlicer ─ M141 S[Target] ... PRINT_START ... CHAMBER=[Minimal]
                 │
Klipper ─ [heater_generic chamber_heater] ◄─ cc_heater plugin (virtual pin + sensor)
                 │  HTTP POST /api/klipper/sync, once per second
                 ▼    {target, chamber_temp, klippy}
Heater ESP32 ─ hysteresis control on the printer chamber sensor (or its own DS18B20)
                 replies {temp, heating, fan, fault, ...}
```

- **Klipper owns the target**, the **heater owns the regulation**.
- Network I/O runs on a background thread; Klipper never blocks on the heater.
- **Fail-safe:** the heater switches itself off when it has not heard from
  Klipper for 10 s (crashed host, WiFi loss, Klipper restart). On an orderly
  shutdown or restart the plugin also sends target 0.
- **Faults** (no reply for 10 s, sensor failure, over-temperature, heater not in
  Klipper mode, or `verify_heater` seeing no temperature rise) switch the heater
  off and **pause** the print (`fault_action: pause`, alternatives `warn` or
  `shutdown`). Turning the heater off in its own web UI only produces a warning.
- **Heater switched off or unreachable:** nothing happens as long as no target
  is set — prints without chamber heating are not affected. The
  `chamber_heater` card in Fluidd and on the display shows **0 °C**, the console
  says `Chamber heater offline (<host>)` once, and `Chamber heater connected`
  when it is back (it reconnects by itself). Setting a target while it is
  offline (e.g. a filament with chamber temperature control) raises the fault
  above after 10 s.

## Installation (COSMOS)

COSMOS loads Kalico plugins from `/etc/klipper/plugins`, which survives
firmware updates. Keeping the plugin itself in the config folder lets you update
it from Fluidd/Moonraker without SSH, so it is linked from there.

1. **Heater:** open its web UI → *Settings* → *Printer Connection Profile* →
   **Klipper / COSMOS** → *Save* (the heater restarts). Leave
   *Temperature Control Source* on **Printer chamber sensor** unless you want to
   regulate on the heater's own DS18B20. Reserve the heater's IP address in your
   router.
2. **Upload the plugin** into the printer config folder as
   `cc_heater/__init__.py` (Fluidd → *Configuration* → create folder `cc_heater`,
   upload `__init__.py`), and upload [`cc_heater.cfg`](cc_heater.cfg) next to
   `printer.cfg`. Set `host:` in `cc_heater.cfg` to the heater's IP address.
3. **Link the plugin** over SSH (once):

   ```sh
   ln -s /etc/klipper/config/cc_heater /etc/klipper/plugins/cc_heater
   ```

4. **Enable it:** add this line to `printer.cfg`, below the COSMOS includes and
   above the `SAVE_CONFIG` block, then restart Klipper:

   ```ini
   [include cc_heater.cfg]
   ```

5. **Display (optional):** add this to `grumpyscreen.cfg` and restart the screen
   (`RUN_SHELL_COMMAND CMD=GUI_STOP`, then `RUN_SHELL_COMMAND CMD=GUI_START`):

   ```ini
   [monitored_sensor "heater_generic chamber_heater"]
   display_name: Heater
   color: orange
   controllable: true
   ```

6. **Check:** run `CC_HEATER_STATUS` in the console. It should report
   `connected`.

**Updating the plugin:** replace `cc_heater/__init__.py` and restart the
Klipper *service* (Fluidd → power menu → *Services* → *klipper* → *Restart*,
or `/etc/init.d/klipper restart` over SSH). A `RESTART` / `FIRMWARE_RESTART`
only reloads the configuration and keeps the old plugin code in memory.

On plain Klipper/Kalico without COSMOS, place the `cc_heater` folder in
`klippy/plugins/` (Kalico) or copy `__init__.py` as `klippy/extras/cc_heater.py`
(Klipper). If your config has no `M191` macro, set `register_m191: True` in
`[cc_heater]` and drop the `[gcode_macro M191]` section from `cc_heater.cfg`.

## OrcaSlicer

1. *Printer settings → Basic information → Accessory*: enable
   **Support control chamber temperature** (the COSMOS profile already does).
2. *Printer settings → Machine G-code → Machine start G-code*: add the `M141`
   line at the very top and pass the minimal temperature to `PRINT_START`
   (COSMOS profile shown):

   ```gcode
   {if activate_chamber_temp_control[initial_tool] and overall_chamber_temperature > 0}M141 S[overall_chamber_temperature] ; chamber heater: heat to Target
   {endif}M104 S0 ; Stops OrcaSlicer from sending temp waits separately

   M140 S0

   SET_PRINT_STATS_INFO TOTAL_LAYER=[total_layer_count]

   PRINT_START EXTRUDER=[first_layer_temperature] BED=[first_layer_bed_temperature] CHAMBER={if activate_chamber_temp_control[initial_tool]}{min(chamber_minimal_temperature[initial_tool], overall_chamber_temperature)}{else}0{endif} TOOL={initial_tool}
   ```

   The heater only runs for filaments with **Activate temperature control**
   enabled (Orca fills `overall_chamber_temperature` even when it is off).
   Because the start G-code contains `M141`, Orca does not insert its own
   blocking `M191` in front of it. Orca adds `M141 S0` at the end of the file,
   and `PRINT_END` switches all heaters off anyway.
3. *Filament settings → Temperatures → Chamber temperature*: enable
   **Activate temperature control** and set **Target** and **Minimal**, e.g.
   Target 45 °C / Minimal 30 °C for ABS. With Minimal 0 the print starts
   immediately and the chamber heats up during the print.

`M191` waits at most one hour (`T=` parameter, `wait_timeout`), then prints
anyway. Cancelling the print aborts the wait.

## Configuration reference (`[cc_heater]`)

| Option | Default | Description |
|---|---|---|
| `host` | — | Heater IP address or hostname |
| `port` | `80` | Heater HTTP port |
| `heater` | `chamber_heater` | Name of the `[heater_generic]` using `cc_heater:pwm` |
| `chamber_sensor` | `temperature_sensor chamber` | Printer sensor forwarded to the heater |
| `max_target` | `65` | Targets above this are clamped |
| `wait_timeout` | `3600` | Default timeout of `CHAMBER_HEATER_WAIT` / `M191`, seconds |
| `fault_action` | `pause` | `pause`, `warn` or `shutdown` on a heater fault |
| `link_timeout` | `10` | Seconds without a reply before a fault is raised |
| `sync_interval` | `1` | Seconds between syncs |
| `request_timeout` | `2` | HTTP timeout, seconds |
| `register_m191` | `False` | Register a native `M191` (only without an `M191` macro) |

G-code commands: `M141 S<temp>`,
`CHAMBER_HEATER_WAIT MINIMUM=<temp> [TARGET=<temp>] [TIMEOUT=<s>]`,
`CC_HEATER_STATUS`, plus the standard `SET_HEATER_TEMPERATURE
HEATER=chamber_heater TARGET=<temp>`.

The printer object `cc_heater` exposes `connected`, `heating`, `fan`, `fault`,
`device_fault`, `source`, `heater_temp`, `chamber_temp`, `device_target`,
`max_temp`, `firmware` and `last_error` for macros and dashboards.

## License

The plugin is distributed under the GNU GPLv3, like Klipper and Kalico. The
approach (virtual pin + sensor factory feeding a `heater_generic`) follows the
COSMOS DragonBreath addon for the Panda Breath.
