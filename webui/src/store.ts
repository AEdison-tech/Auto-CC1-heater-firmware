import { createSignal } from 'solid-js'

export const [debugMode, setDebugMode] = createSignal(false)
export const [debugBedTemp, setDebugBedTemp] = createSignal(0.0)
export const [debugHeaterTemp, setDebugHeaterTemp] = createSignal(0.0)
export const [debugChamberTemp, setDebugChamberTemp] = createSignal(0.0)
export const [debugIsPrinting, setDebugIsPrinting] = createSignal(false)
