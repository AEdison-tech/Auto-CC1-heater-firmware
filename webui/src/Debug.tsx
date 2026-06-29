import { createSignal } from 'solid-js'
import { debugBedTemp, setDebugBedTemp, debugHeaterTemp, setDebugHeaterTemp, debugChamberTemp, setDebugChamberTemp, debugIsPrinting, setDebugIsPrinting } from './store'

function Debug() {
  const [success, setSuccess] = createSignal(false)
  const [error, setError] = createSignal('')

  const handleApply = async () => {
    try {
      setError('')
      setSuccess(false)
      const res = await fetch('/debug_override', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ isPrinting: debugIsPrinting(), bedTemp: debugBedTemp(), heaterTemp: debugHeaterTemp(), chamberTemp: debugChamberTemp() }),
      })
      if (!res.ok) throw new Error(`${res.status} ${res.statusText}`)
      setSuccess(true)
      setTimeout(() => setSuccess(false), 3000)
    } catch (err: any) {
      setError(`Error: ${err.message}`)
    }
  }

  return (
    <div class="card">
      <div role="alert" class="mb-6 alert alert-warning alert-soft">
        <span>Debug mode is active!</span>
      </div>

      {error() && <div role="alert" class="mb-4 alert alert-error">{error()}</div>}
      {success() && <div role="alert" class="mb-4 alert alert-success">Override applied!</div>}

      <fieldset class="fieldset">
        <legend class="fieldset-legend">Bed Temperature (°C)</legend>
        <input type="number" class="input" value={debugBedTemp()}
          onInput={(e) => setDebugBedTemp(parseFloat(e.target.value) || 0)}
          min="0" max="120" step="0.5" />
      </fieldset>

      <fieldset class="fieldset mt-4">
        <legend class="fieldset-legend">Heater Temperature (°C)</legend>
        <input type="number" class="input" value={debugHeaterTemp()}
          onInput={(e) => setDebugHeaterTemp(parseFloat(e.target.value) || 0)}
          min="0" max="120" step="0.5" />
      </fieldset>

      <fieldset class="fieldset mt-4">
        <legend class="fieldset-legend">Chamber Temperature (°C)</legend>
        <input type="number" class="input" value={debugChamberTemp()}
          onInput={(e) => setDebugChamberTemp(parseFloat(e.target.value) || 0)}
          min="0" max="120" step="0.5" />
      </fieldset>

      <fieldset class="fieldset mt-4">
        <legend class="fieldset-legend">Printing State</legend>
        <label class="label cursor-pointer">
          <input type="checkbox" class="checkbox checkbox-accent"
            checked={debugIsPrinting()}
            onChange={(e) => setDebugIsPrinting(e.target.checked)} />
        </label>
      </fieldset>

      <button class="btn btn-accent btn-soft mt-6" onClick={handleApply}>
        Apply
      </button>
    </div>
  )
}

export default Debug
