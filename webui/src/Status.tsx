import { createSignal, onMount, onCleanup } from 'solid-js'
import { setDebugMode } from './store'

const PRINT_STATUS_MAP: Record<number, string> = {
  0: 'Idle',
  1: 'Homing',
  2: 'Dropping',
  3: 'Exposing',
  4: 'Lifting',
  5: 'Pausing',
  6: 'Paused',
  7: 'Stopping',
  8: 'Stopped',
  9: 'Complete',
  10: 'File Checking',
  13: 'Printing',
  15: 'Unknown (15)',
  16: 'Heating',
  18: 'Unknown (18)',
  19: 'Unknown (19)',
  20: 'Bed Leveling',
  21: 'Unknown (21)',
}

interface StatusData {
  heaterActive: boolean
  fanActive: boolean
  heaterTemp: number
  bedTemp: number
  chamberTemp: number
  debug: boolean
  elegoo: {
    isConnected: boolean
    isPrinting: boolean
    printStatus: number
  }
}

const defaultStatus: StatusData = {
  heaterActive: false,
  fanActive: false,
  heaterTemp: 0,
  bedTemp: 0,
  chamberTemp: 0,
  debug: false,
  elegoo: {
    isConnected: false,
    isPrinting: false,
    printStatus: 0,
  },
}

function TempCard(props: { label: string; value: number; unit?: string }) {
  return (
    <div class="stat">
      <div class="stat-title">{props.label}</div>
      <div class="stat-value text-primary">
        {props.value.toFixed(1)}{props.unit ?? '°C'}
      </div>
    </div>
  )
}

function StatusBadge(props: { label: string; active: boolean; activeText?: string; inactiveText?: string }) {
  return (
    <div class="stat">
      <div class="stat-title">{props.label}</div>
      <div class={`stat-value ${props.active ? 'text-success' : 'text-base-content/40'}`}>
        {props.active ? (props.activeText ?? 'ON') : (props.inactiveText ?? 'OFF')}
      </div>
    </div>
  )
}

function Status() {
  const [loading, setLoading] = createSignal(true)
  const [status, setStatus] = createSignal<StatusData>(defaultStatus)

  const refresh = async () => {
    try {
      const res = await fetch('/api/status')
      const data = await res.json()
      setStatus(data)
      setDebugMode(data.debug ?? false)
    } catch (_) {
      // keep last known state on error
    }
    setLoading(false)
  }

  onMount(() => {
    refresh()
    const id = setInterval(refresh, 2500)
    onCleanup(() => clearInterval(id))
  })

  return (
    <div>
      {loading() ? (
        <p><span class="loading loading-spinner loading-xl"></span></p>
      ) : (
        <div class="flex flex-col gap-6">

          {/* Temperature + heater state */}
          <div class="stats w-full shadow bg-base-200">
            <TempCard label="Heater Temperature" value={status().heaterTemp} />
            <TempCard label="Bed Temperature" value={status().bedTemp} />
            <TempCard label="Chamber Temperature" value={status().chamberTemp} />
            <StatusBadge label="Heater" active={status().heaterActive} />
            <StatusBadge label="Fan" active={status().fanActive} />
          </div>

          {/* Printer connection + print state */}
          <div class="card w-full bg-base-200 card-sm shadow-sm">
            <div class="card-body">
              <h2 class="card-title">Printer</h2>
              <div class="text-sm flex gap-6 flex-wrap">
                <div>
                  <h3 class="font-bold">Connection</h3>
                  <p class={status().elegoo.isConnected ? 'text-success' : 'text-error'}>
                    {status().elegoo.isConnected ? 'Connected' : 'Disconnected'}
                  </p>
                </div>
                <div>
                  <h3 class="font-bold">Status</h3>
                  <p>{PRINT_STATUS_MAP[status().elegoo.printStatus] ?? status().elegoo.printStatus}</p>
                </div>
                <div>
                  <h3 class="font-bold">Printing</h3>
                  <p>{status().elegoo.isPrinting ? 'Yes' : 'No'}</p>
                </div>
              </div>
            </div>
          </div>

        </div>
      )}
    </div>
  )
}

export default Status
