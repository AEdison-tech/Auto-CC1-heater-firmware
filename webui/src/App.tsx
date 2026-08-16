import { A, useLocation } from '@solidjs/router'
import { ParentProps, Show, onMount } from 'solid-js'
import { debugMode, setDebugMode } from './store'

function App(props: ParentProps) {
  const location = useLocation()

  onMount(async () => {
    try {
      const res = await fetch('/get_settings')
      if (res.ok) {
        const s = await res.json()
        setDebugMode(s.debug ?? false)
      }
    } catch (_) {}
  })

  const isActive = (path: string) => {
    const pathname = location.pathname
    if (path === '/' || path === '/status') {
      return pathname === '/' || pathname === '/status'
    }
    return pathname === path
  }

  return (
    <div class="flex flex-col items-center min-h-screen pt-10 bg-base-200">
      <h1 class="text-xl font-bold w-full max-w-5xl pl-1 pb-4">Elegoo Centauri Carbon <span class="text-accent">Chamber Heater</span></h1>
      <div class="tabs tabs-lift w-full max-w-5xl">

        <A href="/status" class={`tab ${isActive('/status') ? 'tab-active' : ''}`}>
          Status
        </A>

        <A href="/settings" class={`tab ${isActive('/settings') ? 'tab-active' : ''}`}>
          Settings
        </A>

        <A href="/logs" class={`tab ${isActive('/logs') ? 'tab-active' : ''}`}>
          Logs
        </A>

        <A href="/firmware" class={`tab ${isActive('/firmware') ? 'tab-active' : ''}`}>
          Update
        </A>

        <A href="/about" class={`tab ${isActive('/about') ? 'tab-active' : ''}`}>
          About
        </A>

        <Show when={debugMode()}>
          <A href="/debug" class={`tab ${isActive('/debug') ? 'tab-active' : ''}`}>
            Debug
          </A>
        </Show>

      </div>

      <div class="w-full max-w-5xl flex-1">
        <div class="bg-base-100 border-base-300 p-6 min-h-full">
          {props.children}
        </div>
      </div>
    </div>
  )
}

export default App
