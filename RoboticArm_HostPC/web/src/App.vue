<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from 'vue'

const consoleLines = ref<string[]>([])
const consoleRef = ref<HTMLElement | null>(null)
const camRef = ref<HTMLVideoElement | null>(null)
const wsState = ref<'disconnected' | 'connecting' | 'open' | 'error'>('disconnected')
const keysHeld = ref<Record<string, boolean>>({})
const lastCmd = ref('')

const cameraSrc = import.meta.env.VITE_CAMERA_URL as string | undefined

const hostDisplay = typeof window !== 'undefined' ? window.location.host : '—'

const wsUrl = computed(() => {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${proto}//${location.host}/ws`
})

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

function pushLine(line: string, cls = '') {
  const t = new Date().toISOString().replace('T', ' ').slice(0, 23)
  consoleLines.value.push(`[${t}] ${line}`)
  if (consoleLines.value.length > 500) consoleLines.value.splice(0, consoleLines.value.length - 500)
  requestAnimationFrame(() => {
    if (consoleRef.value) consoleRef.value.scrollTop = consoleRef.value.scrollHeight
  })
}

function sendKey(key: string, down: boolean) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'key', key, down }))
  }
}

function onKeyEv(e: KeyboardEvent, down: boolean) {
  const k = e.key.toLowerCase()
  const map = ['w', 'a', 's', 'd', 'q', 'e']
  if (!map.includes(k)) return
  e.preventDefault()
  if (down) {
    if (keysHeld.value[k]) return
    keysHeld.value[k] = true
  } else {
    delete keysHeld.value[k]
  }
  sendKey(k, down)
  const names: Record<string, string> = {
    w: 'forward',
    s: 'reverse',
    a: 'strafe_left',
    d: 'strafe_right',
    q: 'rotate_ccw',
    e: 'rotate_cw',
  }
  if (down) lastCmd.value = names[k] ?? k
  else if (!Object.keys(keysHeld.value).length) lastCmd.value = 'idle'
}

function connectWs() {
  wsState.value = 'connecting'
  try {
    ws = new WebSocket(wsUrl.value)
  } catch {
    wsState.value = 'error'
    return
  }
  ws.onopen = () => {
    wsState.value = 'open'
    pushLine('INFO  WebSocket session established', 'ok')
  }
  ws.onclose = () => {
    wsState.value = 'disconnected'
    pushLine('WARN  WebSocket closed — reconnecting in 2s…', 'warn')
    reconnectTimer = setTimeout(connectWs, 2000)
  }
  ws.onerror = () => {
    wsState.value = 'error'
  }
  ws.onmessage = (ev) => {
    try {
      const j = JSON.parse(ev.data as string)
      if (j.type === 'log' && typeof j.line === 'string') pushLine(j.line)
      else if (j.type === 'ack' && typeof j.msg === 'string') pushLine(`ACK   ${j.msg}`)
    } catch {
      pushLine(String(ev.data))
    }
  }
}

function bindCamera() {
  if (!camRef.value || !cameraSrc) return
  camRef.value.src = cameraSrc
  camRef.value.muted = true
  camRef.value.play().catch(() => pushLine('WARN  Camera stream failed (check VITE_CAMERA_URL / CORS)', 'warn'))
}

onMounted(() => {
  window.addEventListener('keydown', (e) => onKeyEv(e, true))
  window.addEventListener('keyup', (e) => onKeyEv(e, false))
  connectWs()
  bindCamera()
})

onUnmounted(() => {
  window.removeEventListener('keydown', (e) => onKeyEv(e, true))
  window.removeEventListener('keyup', (e) => onKeyEv(e, false))
  if (reconnectTimer) clearTimeout(reconnectTimer)
  ws?.close()
})

const statusColor = computed(() => {
  switch (wsState.value) {
    case 'open':
      return 'text-pve-ok'
    case 'connecting':
      return 'text-pve-warn'
    case 'error':
      return 'text-pve-err'
    default:
      return 'text-pve-muted'
  }
})
</script>

<template>
  <div
    class="flex h-full min-h-[600px] flex-col bg-pve-bg font-ui text-pve-text"
    tabindex="0"
  >
    <!-- Top bar: PVE-style -->
    <header
      class="flex h-9 shrink-0 items-center border-b border-pve-border bg-gradient-to-b from-[#454545] to-[#3a3a3a] px-3 text-sm shadow"
    >
      <span class="font-semibold tracking-tight text-white">OmniRoam</span>
      <span class="mx-2 text-pve-muted">|</span>
      <span class="text-pve-muted">Host Console</span>
      <span class="ml-6 font-mono text-xs text-pve-accent2">{{ hostDisplay }}</span>
      <div class="ml-auto flex items-center gap-3 font-mono text-xs">
        <span :class="statusColor">● {{ wsState }}</span>
      </div>
    </header>

    <!-- Main 16:9 friendly row -->
    <div class="flex min-h-0 flex-1 flex-col lg:flex-row">
      <!-- Left: camera -->
      <section
        class="flex min-h-[240px] w-full shrink-0 flex-col border-b border-pve-border lg:min-h-0 lg:w-[42%] lg:border-b-0 lg:border-r"
      >
        <div class="pve-panel-title flex items-center justify-between">
          <span>Video — USB Camera</span>
          <span v-if="!cameraSrc" class="normal-case text-pve-warn">No VITE_CAMERA_URL</span>
        </div>
        <div class="relative flex min-h-0 flex-1 items-center justify-center bg-black">
          <video
            v-if="cameraSrc"
            ref="camRef"
            class="max-h-full max-w-full object-contain"
            playsinline
            autoplay
            muted
          />
          <div
            v-else
            class="flex flex-col items-center gap-2 p-8 text-center text-pve-muted"
          >
            <div class="h-32 w-full max-w-md border border-dashed border-pve-border bg-pve-panel/50" />
            <p class="max-w-sm font-mono text-xs">
              Set <code class="text-pve-accent2">VITE_CAMERA_URL</code> to MJPEG / HLS endpoint, or bind device via
              backend relay.
            </p>
          </div>
        </div>
      </section>

      <!-- Right: console -->
      <section class="flex min-h-0 min-w-0 flex-1 flex-col">
        <div class="pve-panel-title">System log — ROS / Serial / Control</div>
        <pre
          ref="consoleRef"
          class="m-0 flex-1 overflow-auto bg-[#141414] p-3 font-mono text-[11px] leading-relaxed text-[#b8e0b8]"
          >{{ consoleLines.join('\n') }}</pre
        >
      </section>
    </div>

    <!-- Bottom: operations (WASD + scheme) -->
    <footer
      class="shrink-0 border-t border-pve-border bg-pve-panel px-4 py-3 shadow-[inset_0_1px_0_#4a4a4a]"
    >
      <div class="mb-2 text-xs font-semibold uppercase tracking-wider text-pve-muted">
        Operation — chassis (holonomic)
      </div>
      <div class="flex flex-wrap items-center gap-4 text-sm">
        <div class="flex items-center gap-2">
          <span class="pve-kbd">W</span>
          <span class="text-pve-muted">Forward</span>
        </div>
        <div class="flex items-center gap-2">
          <span class="pve-kbd">S</span>
          <span class="text-pve-muted">Reverse</span>
        </div>
        <div class="flex items-center gap-2">
          <span class="pve-kbd">A</span>
          <span class="text-pve-muted">Strafe left</span>
        </div>
        <div class="flex items-center gap-2">
          <span class="pve-kbd">D</span>
          <span class="text-pve-muted">Strafe right</span>
        </div>
        <div class="flex items-center gap-2 border-l border-pve-border pl-4">
          <span class="pve-kbd">Q</span>
          <span class="text-pve-muted">Rotate CCW</span>
        </div>
        <div class="flex items-center gap-2">
          <span class="pve-kbd">E</span>
          <span class="text-pve-muted">Rotate CW</span>
        </div>
        <div class="ml-auto font-mono text-xs text-pve-accent">
          Active: <span class="text-white">{{ lastCmd || '—' }}</span>
        </div>
      </div>
    </footer>
  </div>
</template>
