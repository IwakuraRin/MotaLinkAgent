<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { Terminal } from '@xterm/xterm'
import { FitAddon } from '@xterm/addon-fit'
import '@xterm/xterm/css/xterm.css'
import { t } from '../../i18n'

const containerRef = ref<HTMLElement | null>(null)
let term: Terminal | null = null
let fit: FitAddon | null = null
let ws: WebSocket | null = null
let disposed = false
const enc = new TextEncoder()

const shellURL = computed(() => {
  const p = location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${p}//${location.host}/ws/shell`
})

function sendResize() {
  if (!term || !ws || ws.readyState !== WebSocket.OPEN) return
  try {
    ws.send(JSON.stringify({ type: 'resize', cols: term.cols, rows: term.rows }))
  } catch {
    /* ignore */
  }
}

function disconnectShell() {
  const s = ws
  ws = null
  if (s) {
    try {
      s.close()
    } catch {
      /* ignore */
    }
  }
}

function connectShell() {
  disconnectShell()
  if (!term || disposed) return

  let socket: WebSocket
  try {
    socket = new WebSocket(shellURL.value)
  } catch {
    term.writeln(`\r\n\x1b[31m${t('desktop.shell.connectFail')}\x1b[0m`)
    return
  }
  ws = socket
  socket.binaryType = 'arraybuffer'

  socket.onopen = () => {
    if (disposed || ws !== socket) return
    fit?.fit()
    sendResize()
  }

  socket.onmessage = (ev) => {
    if (!term || disposed) return
    if (ev.data instanceof ArrayBuffer) {
      term.write(new Uint8Array(ev.data))
    } else if (typeof ev.data === 'string') {
      term.write(ev.data)
    }
  }

  // Browsers rarely expose details on error; onclose always follows with a code.
  socket.onerror = () => {}

  socket.onclose = (ev) => {
    if (ws === socket) ws = null
    if (!term || disposed) return
    if (ev.code === 1000) return
    const detail = ev.reason ? ` (${ev.code}: ${ev.reason})` : ` (${ev.code})`
    term.writeln(`\r\n\x1b[33m${t('desktop.shell.disconnected')}${detail}\x1b[0m`)
    if (import.meta.env.DEV) {
      term.writeln(`\x1b[90m${t('desktop.shell.devHint')}\x1b[0m`)
    }
  }
}

onMounted(async () => {
  await nextTick()
  const el = containerRef.value
  if (!el) return

  term = new Terminal({
    cursorBlink: true,
    fontSize: 13,
    fontFamily: '"JetBrains Mono", Consolas, monospace',
    theme: {
      background: '#0c0e12',
      foreground: '#c8d0dc',
      cursor: '#7aa2f7',
    },
  })
  fit = new FitAddon()
  term.loadAddon(fit)
  term.open(el)
  fit.fit()

  term.writeln(`\x1b[36m${t('desktop.shell.banner')}\x1b[0m\r\n`)

  term.onData((data) => {
    if (!ws || ws.readyState !== WebSocket.OPEN) return
    try {
      ws.send(enc.encode(data))
    } catch {
      /* ignore */
    }
  })

  connectShell()

  const ro = new ResizeObserver(() => {
    fit?.fit()
    sendResize()
  })
  ro.observe(el)
  ;(el as HTMLElement & { _ro?: ResizeObserver })._ro = ro
})

onUnmounted(() => {
  disposed = true
  const el = containerRef.value as (HTMLElement & { _ro?: ResizeObserver }) | null
  if (el?._ro) el._ro.disconnect()
  disconnectShell()
  term?.dispose()
  term = null
  fit = null
})
</script>

<template>
  <div ref="containerRef" class="h-full min-h-[160px] w-full" />
</template>
