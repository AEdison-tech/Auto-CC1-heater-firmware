import { createSignal, onMount } from 'solid-js'
import { setDebugMode } from './store'

function Settings() {
  const [ssid, setSsid] = createSignal('')
  const [password, setPassword] = createSignal('')
  const [elegooip, setElegooip] = createSignal('')
  const [activationTemp, setActivationTemp] = createSignal(85.0)
  const [controlSource, setControlSource] = createSignal('heater')
  const [requirePrinting, setRequirePrinting] = createSignal(false)
  const [targetTemp, setTargetTemp] = createSignal(60.0)
  const [hysteresis, setHysteresis] = createSignal(5.0)
  const [enabled, setEnabled] = createSignal(true)
  const [debug, setDebug] = createSignal(false)
  const [apMode, setApMode] = createSignal<boolean | null>(null)
  const [loading, setLoading] = createSignal(true)
  const [error, setError] = createSignal('')
  const [saveSuccess, setSaveSuccess] = createSignal(false)

  onMount(async () => {
    try {
      setLoading(true)
      const res = await fetch('/get_settings')
      if (!res.ok) throw new Error(`${res.status} ${res.statusText}`)
      const s = await res.json()

      setSsid(s.ssid ?? '')
      setPassword('')
      setElegooip(s.elegooip ?? '')
      setActivationTemp(s.activation_temp ?? 85.0)
      setControlSource(s.control_source ?? 'heater')
      setRequirePrinting(s.require_printing ?? false)
      setTargetTemp(s.target_temp ?? 60.0)
      setHysteresis(s.hysteresis ?? 5.0)
      setEnabled(s.enabled ?? true)
      setApMode(s.ap_mode ?? false)
      setDebug(s.debug ?? false)
      setDebugMode(s.debug ?? false)
      setError('')
    } catch (err: any) {
      setError(`Error loading settings: ${err.message}`)
    } finally {
      setLoading(false)
    }
  })

  const handleSave = async () => {
    try {
      setSaveSuccess(false)
      setError('')

      const body = {
        ssid: ssid(),
        passwd: password(),
        ap_mode: false,
        elegooip: elegooip(),
        activation_temp: activationTemp(),
        control_source: controlSource(),
        require_printing: requirePrinting(),
        target_temp: targetTemp(),
        hysteresis: hysteresis(),
        enabled: enabled(),
        debug: debug(),
      }

      const res = await fetch('/update_settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      })

      if (!res.ok) throw new Error(`${res.status} ${res.statusText}`)
      setDebugMode(debug())
      setSaveSuccess(true)
      setTimeout(() => setSaveSuccess(false), 3000)
    } catch (err: any) {
      setError(`Error saving settings: ${err.message}`)
    }
  }

  return (
    <div>
      {loading() ? (
        <p>Loading settings... <span class="loading loading-spinner loading-xl"></span></p>
      ) : (
        <div>
          {error() && (
            <div role="alert" class="mb-4 alert alert-error">{error()}</div>
          )}
          {saveSuccess() && (
            <div role="alert" class="mb-4 alert alert-success">Settings saved!</div>
          )}

          {/* WiFi */}
          <h2 class="text-lg font-bold mb-4">WiFi Settings</h2>

          {apMode() ? (
            <div>
              <fieldset class="fieldset">
                <legend class="fieldset-legend">SSID</legend>
                <input type="text" class="input" value={ssid()}
                  onInput={(e) => setSsid(e.target.value)}
                  placeholder="WiFi network name" />
              </fieldset>

              <fieldset class="fieldset">
                <legend class="fieldset-legend">Password</legend>
                <input type="password" class="input" value={password()}
                  onInput={(e) => setPassword(e.target.value)}
                  placeholder="WiFi password" />
              </fieldset>

              <div role="alert" class="mt-4 alert alert-info alert-soft">
                <span>
                  After changing WiFi you may need to reconnect. If connection fails the device
                  reverts to AP mode — connect to <strong>ElegooChamberHeater</strong> and visit{' '}
                  <a class="link link-accent" href="http://192.168.4.1">192.168.4.1</a>.
                  With mDNS: <a class="link link-accent" href="http://ccheater.local">ccheater.local</a>
                </span>
              </div>
            </div>
          ) : (
            <button class="btn" onClick={() => setApMode(true)}>Change WiFi network</button>
          )}

          {/* Printer */}
          <h2 class="text-lg font-bold mb-4 mt-10">Printer</h2>

          <fieldset class="fieldset">
            <legend class="fieldset-legend">Elegoo Centauri Carbon IP Address</legend>
            <input type="text" class="input" value={elegooip()}
              onInput={(e) => setElegooip(e.target.value)}
              placeholder="xxx.xxx.xxx.xxx" />
          </fieldset>

          {/* Heater */}
          <h2 class="text-lg font-bold mb-4 mt-10">Heater Settings</h2>

          <fieldset class="fieldset">
            <legend class="fieldset-legend">Activation Temperature (°C)</legend>
            <input type="number" class="input" value={activationTemp()}
              onInput={(e) => setActivationTemp(parseFloat(e.target.value) || 30)}
              min="0" max="120" step="0.5" />
            <p class="label">Heating starts when the bed temperature reaches this value{requirePrinting() ? ' AND a print is active' : ''}.</p>
          </fieldset>

          <fieldset class="fieldset mt-2">
            <legend class="fieldset-legend">Start Only When Printing</legend>
            <label class="label cursor-pointer">
              <input type="checkbox" class="checkbox checkbox-accent"
                checked={requirePrinting()}
                onChange={(e) => setRequirePrinting(e.target.checked)} />
              <span class="label-text">When checked, heater activates only during an active print. When unchecked, heater activates based on temperature alone.</span>
            </label>
          </fieldset>

          <fieldset class="fieldset">
            <legend class="fieldset-legend">Temperature Control Source</legend>
            <select class="select" value={controlSource()}
              onChange={(e) => setControlSource(e.target.value)}>
              <option value="heater">Heater Temperature</option>
              <option value="chamber">Chamber Temperature</option>
            </select>
            <p class="label">Which temperature is compared against the target to control the heater on/off.</p>
          </fieldset>

          <fieldset class="fieldset">
            <legend class="fieldset-legend">Target Box Temperature (°C)</legend>
            <input type="number" class="input" value={targetTemp()}
              onInput={(e) => setTargetTemp(parseFloat(e.target.value) || 35)}
              min="0" max="80" step="0.5" />
            <p class="label">Desired temperature inside the enclosure.</p>
          </fieldset>

          <fieldset class="fieldset">
            <legend class="fieldset-legend">Hysteresis (°C)</legend>
            <input type="number" class="input" value={hysteresis()}
              onInput={(e) => setHysteresis(parseFloat(e.target.value) || 2)}
              min="0.5" max="10" step="0.5" />
            <p class="label">
              Heater turns ON below <strong>{(targetTemp() - hysteresis()).toFixed(1)} °C</strong>,
              turns OFF above <strong>{(targetTemp() + hysteresis()).toFixed(1)} °C</strong>.
            </p>
          </fieldset>

          <fieldset class="fieldset mt-2">
            <legend class="fieldset-legend">Enabled</legend>
            <label class="label cursor-pointer">
              <input type="checkbox" class="checkbox checkbox-accent"
                checked={enabled()}
                onChange={(e) => setEnabled(e.target.checked)} />
              <span class="label-text">Enable automatic heater control. When unchecked, heater stays off regardless of conditions.</span>
            </label>
          </fieldset>

          <fieldset class="fieldset mt-2">
            <legend class="fieldset-legend">Debug Mode</legend>
            <label class="label cursor-pointer">
              <input type="checkbox" class="checkbox checkbox-accent"
                checked={debug()}
                onChange={(e) => setDebug(e.target.checked)} />
              <span class="label-text">Show Debug.</span>
            </label>
          </fieldset>

          <button class="btn btn-accent btn-soft mt-10" onClick={handleSave}>
            Save Settings
          </button>
        </div>
      )}
    </div>
  )
}

export default Settings
