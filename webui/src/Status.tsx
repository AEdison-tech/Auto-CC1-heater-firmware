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

interface KlipperStatus {
  linked: boolean
  everLinked: boolean
  target: number
  klippyReady: boolean
  lastSyncAgoMs: number
  syncCount: number
  fault: string
  controlTemp: number | null
  chamberTemp: number | null
}

const FAULT_TEXT: Record<string, string> = {
  disabled: 'Heater control disabled in settings',
  sensor: 'Control sensor has no valid reading',
  overtemp: 'Over-temperature',
}

interface StatusData {
  profile: string
  controlSource: string
  maxChamberTemp: number
  heaterSensorOk: boolean
  klipper: KlipperStatus
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
  profile: 'elegoo',
  controlSource: 'heater',
  maxChamberTemp: 65,
  heaterSensorOk: true,
  klipper: {
    linked: false,
    everLinked: false,
    target: 0,
    klippyReady: false,
    lastSyncAgoMs: 0,
    syncCount: 0,
    fault: '',
    controlTemp: null,
    chamberTemp: null,
  },
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
      setStatus({ ...defaultStatus, ...data, klipper: { ...defaultStatus.klipper, ...(data.klipper ?? {}) } })
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
            {status().profile !== 'klipper' && (
              <TempCard label="Bed Temperature" value={status().bedTemp} />
            )}
            <TempCard label="Chamber Temperature" value={status().chamberTemp} />
            <StatusBadge label="Heater" active={status().heaterActive} />
            <StatusBadge label="Fan" active={status().fanActive} />
          </div>

          {/* Klipper link (profile "klipper") */}
          {status().profile === 'klipper' && (
          <div class="card w-full bg-base-200 card-sm shadow-sm">
            <div class="card-body">
              <h2 class="card-title">Klipper</h2>
              <div class="text-sm flex gap-6 flex-wrap">
                <div>
                  <h3 class="font-bold">Link</h3>
                  <p class={status().klipper.linked ? 'text-success' : 'text-error'}>
                    {status().klipper.linked
                      ? 'Connected'
                      : status().klipper.everLinked ? 'Lost' : 'Waiting for plugin'}
                  </p>
                </div>
                <div>
                  <h3 class="font-bold">Klipper</h3>
                  <p>{status().klipper.linked ? (status().klipper.klippyReady ? 'Ready' : 'Not ready') : '—'}</p>
                </div>
                <div>
                  <h3 class="font-bold">Target</h3>
                  <p>
                    {status().klipper.linked && status().klipper.target > 0
                      ? `${Math.min(status().klipper.target, status().maxChamberTemp).toFixed(1)} °C`
                      : 'Off'}
                  </p>
                </div>
                <div>
                  <h3 class="font-bold">Control</h3>
                  <p>
                    {status().controlSource === 'chamber' ? 'Chamber sensor' : 'Heater sensor'}
                    {status().klipper.controlTemp !== null ? ` · ${status().klipper.controlTemp!.toFixed(1)} °C` : ''}
                  </p>
                </div>
                <div>
                  <h3 class="font-bold">Last sync</h3>
                  <p>{status().klipper.everLinked ? `${(status().klipper.lastSyncAgoMs / 1000).toFixed(1)} s ago` : '—'}</p>
                </div>
              </div>
              {status().klipper.fault && (
                <div role="alert" class="mt-2 alert alert-warning alert-soft">
                  {FAULT_TEXT[status().klipper.fault] ?? status().klipper.fault}
                </div>
              )}
            </div>
          </div>
          )}

          {/* Printer connection + print state */}
          {status().profile !== 'klipper' && (
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
          )}

        </div>
      )}
    </div>
  )
}

export default Status
