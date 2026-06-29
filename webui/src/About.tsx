import { createSignal, onMount } from 'solid-js'

interface VersionInfo {
  firmware_version: string
  chip_family: string
  build_date: string
  build_time: string
}

function About() {
  const [version, setVersion] = createSignal<VersionInfo | null>(null)

  onMount(async () => {
    try {
      const res = await fetch('/version')
      if (res.ok) setVersion(await res.json())
    } catch (_) {}
  })

  return (
    <div>
      {version() && (
        <div class="stats shadow bg-base-200 mb-6">
          <div class="stat">
            <div class="stat-title">Firmware</div>
            <div class="stat-value text-base font-mono">{version()!.firmware_version}</div>
          </div>
          <div class="stat">
            <div class="stat-title">Chip</div>
            <div class="stat-value text-base font-mono">{version()!.chip_family}</div>
          </div>
          <div class="stat">
            <div class="stat-title">Built</div>
            <div class="stat-value text-base font-mono">{version()!.build_date} {version()!.build_time}</div>
          </div>
        </div>
      )}

      <p>
        Automatic chamber heater controller for Elegoo Centauri Carbon FDM printers.
        Monitors printer status via the SDCP WebSocket protocol and controls a heating element,
        fan, and status LED based on configurable temperature thresholds.
      </p>
      <h2 class="text-lg font-bold mt-4 mb-4">Credits</h2>
      <ul class="flex flex-col gap-1">
        <li><a class="link link-accent" target="_blank" href="https://github.com/QuinnDamerell/OctoPrint-OctoEverywhere">OctoEverywhere</a> — ideas on how to use the Centauri Carbon WebSocket protocol</li>
        <li><a class="link link-accent" target="_blank" href="https://suchmememanyskill.github.io/OpenCentauri/">OpenCentauri</a> — general information about the Centauri Carbon</li>
        <li><a class="link link-accent" target="_blank" href="https://github.com/cbd-tech/SDCP-Smart-Device-Control-Protocol-V3.0.0/tree/main">Smart Device Control Protocol</a> — WS control protocol used by Elegoo Centauri Carbon</li>
        <li><a class="link link-accent" target="_blank" href="https://github.com/bblanchon/ArduinoJson">ArduinoJSON</a> — JSON library</li>
        <li><a class="link link-accent" target="_blank" href="https://github.com/me-no-dev/ESPAsyncWebServer">ESPAsyncWebServer</a> — web server</li>
        <li><a class="link link-accent" target="_blank" href="https://github.com/Links2004/arduinoWebSockets">WebSocket Client</a> — WebSocket client</li>
        <li><a class="link link-accent" target="_blank" href="https://github.com/robtillaart/UUID">UUID</a> — UUID generation</li>
        <li><a class="link link-accent" target="_blank" href="https://github.com/ayushsharma82/ElegantOTA">ElegantOTA</a> — OTA firmware updater</li>
        <li><a class="link link-accent" target="_blank" href="https://www.solidjs.com/">Solid-JS</a> — frontend library</li>
        <li><a class="link link-accent" target="_blank" href="https://tailwindcss.com/">TailwindCSS</a> — CSS framework</li>
        <li><a class="link link-accent" target="_blank" href="https://daisyui.com/">DaisyUI</a> — UI component library</li>
        <li><a class="link link-accent" target="_blank" href="https://vitejs.dev/">Vite</a> — frontend build tool</li>
        <li><a class="link link-accent" target="_blank" href="https://www.typescriptlang.org/">TypeScript</a> — programming language</li>
        <li><a class="link link-accent" target="_blank" href="https://platformio.org/">PlatformIO</a> — embedded build tool</li>
        <li><a class="link link-accent" target="_blank" href="https://github.com/espressif/arduino-esp32">Arduino ESP32</a> — Arduino ESP framework</li>
      </ul>
    </div>
  )
}

export default About
