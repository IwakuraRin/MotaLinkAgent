<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed, watch, nextTick } from 'vue'
import { t, locale, setLocale, type Locale } from './i18n'
import { emptyEdgeLogMap, isTopologyEdgeId, type TopologyEdgeId } from './topology'
import FloatingLogWindow from './components/FloatingLogWindow.vue'
import HostTopologyGraph from './components/HostTopologyGraph.vue'
import WebDesktopRoot from './components/desktop/WebDesktopRoot.vue'

const LS_CAMERA = 'omniroam.camera_url'
const LS_MAXLOG = 'omniroam.console_max_lines'
const LS_KEYBOARD = 'omniroam.keyboard_enabled'

const mainTab = ref<'console' | 'desktop'>(
  typeof sessionStorage !== 'undefined' && sessionStorage.getItem('omniroam.main_tab') === 'desktop'
    ? 'desktop'
    : 'console',
)

watch(mainTab, (v) => {
  if (typeof sessionStorage !== 'undefined') sessionStorage.setItem('omniroam.main_tab', v)
})

type SerialDev = { path: string; target: string; kind: string }

const SERIAL_ROLE_KEYS = ['esp32_uart', 'aux_serial'] as const
type SerialRoleKey = (typeof SERIAL_ROLE_KEYS)[number]

const consoleLines = ref<string[]>([])
const camRef = ref<HTMLVideoElement | null>(null)
const edgeLogs = ref(emptyEdgeLogMap())
const wsState = ref<'disconnected' | 'connecting' | 'open' | 'error'>('disconnected')
const keysHeld = ref<Record<string, boolean>>({})
const lastCmd = ref('')

const settingsOpen = ref(false)
const settingsCameraDraft = ref('')
const appliedCameraUrl = ref('')
const maxLogLines = ref(500)
const keyboardEnabled = ref(true)

const serialDevices = ref<SerialDev[]>([])
const serialListLoading = ref(false)
const serialHostOS = ref('')
const serialRolesDraft = ref<Record<SerialRoleKey, string>>({
  esp32_uart: '',
  aux_serial: '',
})

const envCamera = (import.meta.env.VITE_CAMERA_URL as string | undefined)?.trim() || ''

const cameraSrc = computed(() => {
  const u = appliedCameraUrl.value.trim()
  if (u) return u
  if (envCamera) return envCamera
  return undefined
})

const hostDisplay = typeof window !== 'undefined' ? window.location.host : '—'

const wsUrl = computed(() => {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${proto}//${location.host}/ws`
})

const wsStateLabel = computed(() => {
  const m: Record<string, string> = {
    disconnected: t('ws.disconnected'),
    connecting: t('ws.connecting'),
    open: t('ws.open'),
    error: t('ws.error'),
  }
  return m[wsState.value] ?? wsState.value
})

const lastCmdLabel = computed(() => {
  if (lastCmd.value === 'idle' || !lastCmd.value) return t('op.dash')
  const m: Record<string, string> = {
    forward: t('op.forward'),
    reverse: t('op.reverse'),
    strafe_left: t('op.strafeL'),
    strafe_right: t('op.strafeR'),
    rotate_ccw: t('op.rotCCW'),
    rotate_cw: t('op.rotCW'),
  }
  return m[lastCmd.value] ?? lastCmd.value
})

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null
let wsAllowReconnect = true

function pushLine(line: string): string {
  const tim = new Date().toISOString().replace('T', ' ').slice(0, 23)
  const row = `[${tim}] ${line}`
  consoleLines.value.push(row)
  const cap = Math.max(50, Math.min(5000, maxLogLines.value))
  if (consoleLines.value.length > cap) {
    consoleLines.value.splice(0, consoleLines.value.length - cap)
  }
  return row
}

function ingestLog(line: string, edges?: TopologyEdgeId | TopologyEdgeId[]) {
  const row = pushLine(line)
  const list: TopologyEdgeId[] = !edges ? [] : Array.isArray(edges) ? edges : [edges]
  for (const ed of list) {
    if (!isTopologyEdgeId(ed)) continue
    const prev = edgeLogs.value[ed]
    const next = [...prev, row].slice(-40)
    edgeLogs.value = { ...edgeLogs.value, [ed]: next }
  }
}

function sendKey(key: string, down: boolean) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'key', key, down }))
  }
}

function onKeyEv(e: KeyboardEvent, down: boolean) {
  if (mainTab.value !== 'console') return
  if (!keyboardEnabled.value) return
  if (settingsOpen.value && down) return
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

const onWindowKeyDown = (e: KeyboardEvent) => onKeyEv(e, true)
const onWindowKeyUp = (e: KeyboardEvent) => onKeyEv(e, false)

function connectWs() {
  wsAllowReconnect = true
  wsState.value = 'connecting'
  try {
    ws = new WebSocket(wsUrl.value)
  } catch {
    wsState.value = 'error'
    return
  }
  ws.onopen = () => {
    wsState.value = 'open'
    ingestLog(t('log.wsOpen'), 'e_ws')
  }
  ws.onclose = () => {
    wsState.value = 'disconnected'
    if (!wsAllowReconnect) return
    ingestLog(t('log.wsClosed'), 'e_ws')
    reconnectTimer = setTimeout(connectWs, 2000)
  }
  ws.onerror = () => {
    wsState.value = 'error'
  }
  ws.onmessage = (ev) => {
    try {
      const j = JSON.parse(ev.data as string) as {
        type?: string
        line?: string
        msg?: string
        edge?: string
      }
      if (j.type === 'log' && typeof j.line === 'string') {
        const ed = typeof j.edge === 'string' && isTopologyEdgeId(j.edge) ? j.edge : undefined
        ingestLog(j.line, ed)
      } else if (j.type === 'ack' && typeof j.msg === 'string') {
        const ed =
          typeof j.edge === 'string' && isTopologyEdgeId(j.edge) ? (j.edge as TopologyEdgeId) : 'e_ws'
        ingestLog(`ACK   ${j.msg}`, ed)
      }
    } catch {
      ingestLog(String(ev.data))
    }
  }
}

function reconnectWebSocket() {
  wsAllowReconnect = false
  if (reconnectTimer) {
    clearTimeout(reconnectTimer)
    reconnectTimer = null
  }
  try {
    ws?.close()
  } catch {
    /* ignore */
  }
  ws = null
  ingestLog(t('log.wsManualReconnect'), 'e_ws')
  setTimeout(() => connectWs(), 50)
}

function bindCamera() {
  if (!camRef.value) return
  const src = cameraSrc.value
  if (!src) {
    camRef.value.removeAttribute('src')
    return
  }
  camRef.value.src = src
  camRef.value.muted = true
  camRef.value
    .play()
    .then(() => ingestLog(t('log.videoBound'), 'e_video_ui'))
    .catch(() => ingestLog(t('log.camFail'), 'e_video_ui'))
}

async function hydrateAppliedCameraUrl() {
  let server: { camera_url?: string } | null = null
  async function getServer() {
    if (server) return server
    try {
      const r = await fetch('/api/settings')
      if (r.ok) server = (await r.json()) as { camera_url?: string }
    } catch {
      /* dev without backend */
    }
    return server
  }

  const lsCam = localStorage.getItem(LS_CAMERA)
  if (lsCam !== null) {
    appliedCameraUrl.value = lsCam
  } else {
    const j = await getServer()
    appliedCameraUrl.value = (j && typeof j.camera_url === 'string' ? j.camera_url : '') || envCamera
  }
}

async function refreshSerialDevices() {
  serialListLoading.value = true
  try {
    const r = await fetch('/api/serial/devices')
    if (r.ok) {
      const j = (await r.json()) as { os?: string; devices?: SerialDev[] }
      serialHostOS.value = typeof j.os === 'string' ? j.os : ''
      serialDevices.value = Array.isArray(j.devices) ? j.devices : []
    }
  } catch {
    serialDevices.value = []
  } finally {
    serialListLoading.value = false
  }
}

async function loadSettingsPanelData() {
  try {
    const r = await fetch('/api/settings')
    if (r.ok) {
      const j = (await r.json()) as {
        serial_roles?: Record<string, string>
      }
      const sr = j.serial_roles ?? {}
      serialRolesDraft.value.esp32_uart = sr.esp32_uart ?? ''
      serialRolesDraft.value.aux_serial = sr.aux_serial ?? ''
    }
  } catch {
    /* ignore */
  }
  await refreshSerialDevices()
}

function deviceLabel(d: SerialDev): string {
  if (d.target && d.target !== d.path) return `${d.path} → ${d.target}`
  return d.path
}

function serialRoleTitle(role: SerialRoleKey): string {
  if (role === 'esp32_uart') return t('serial.role.esp32_uart')
  if (role === 'aux_serial') return t('serial.role.aux_serial')
  return role
}

function onLangChange(e: Event) {
  const v = (e.target as HTMLSelectElement).value as Locale
  if (v === 'en' || v === 'zh' || v === 'ko') setLocale(v)
}

async function saveSettings() {
  const url = settingsCameraDraft.value.trim()
  appliedCameraUrl.value = url
  localStorage.setItem(LS_CAMERA, url)
  const serial_roles: Record<string, string> = {}
  for (const k of SERIAL_ROLE_KEYS) {
    const v = serialRolesDraft.value[k]?.trim()
    if (v) serial_roles[k] = v
  }
  try {
    const r = await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ camera_url: url, serial_roles }),
    })
    if (!r.ok) ingestLog(t('log.settingsReject'), 'e_http_api')
  } catch {
    ingestLog(t('log.settingsLocalOnly'), 'e_http_api')
  }
  maxLogLines.value = Math.max(50, Math.min(5000, Math.round(maxLogLines.value)))
  localStorage.setItem(LS_MAXLOG, String(maxLogLines.value))
  await nextTick()
  bindCamera()
  settingsOpen.value = false
}

function clearStoredCamera() {
  settingsCameraDraft.value = ''
  appliedCameraUrl.value = ''
  localStorage.removeItem(LS_CAMERA)
  void saveSettings()
}

watch(settingsOpen, (open) => {
  if (open) {
    settingsCameraDraft.value = appliedCameraUrl.value
    void loadSettingsPanelData()
  }
})

watch(cameraSrc, () => {
  void nextTick(() => bindCamera())
})

watch(keyboardEnabled, (v) => {
  localStorage.setItem(LS_KEYBOARD, v ? '1' : '0')
})

onMounted(async () => {
  const ml = localStorage.getItem(LS_MAXLOG)
  if (ml) {
    const n = parseInt(ml, 10)
    if (!Number.isNaN(n)) maxLogLines.value = n
  }
  keyboardEnabled.value = localStorage.getItem(LS_KEYBOARD) !== '0'

  window.addEventListener('keydown', onWindowKeyDown)
  window.addEventListener('keyup', onWindowKeyUp)
  await hydrateAppliedCameraUrl()
  connectWs()
  await nextTick()
  bindCamera()
})

onUnmounted(() => {
  window.removeEventListener('keydown', onWindowKeyDown)
  window.removeEventListener('keyup', onWindowKeyUp)
  if (reconnectTimer) clearTimeout(reconnectTimer)
  wsAllowReconnect = false
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
    class="relative flex h-full min-h-[600px] flex-col bg-pve-bg font-ui text-pve-text"
    tabindex="0"
  >
    <header
      class="flex h-9 shrink-0 items-center border-b border-pve-border bg-gradient-to-b from-[#454545] to-[#3a3a3a] px-3 text-sm shadow"
    >
      <span class="font-semibold tracking-tight text-white">OmniRoam</span>
      <span class="mx-2 text-pve-muted">|</span>
      <span class="text-pve-muted">{{ t('app.subtitle') }}</span>
      <span class="ml-6 font-mono text-xs text-pve-accent2">{{ hostDisplay }}</span>
      <div class="ml-auto flex items-center gap-3 font-mono text-xs">
        <span :class="statusColor">● {{ wsStateLabel }}</span>
        <button
          type="button"
          class="rounded border border-pve-border bg-pve-panel px-2 py-0.5 text-[11px] font-semibold uppercase tracking-wide text-pve-text shadow hover:bg-pve-header"
          @click="settingsOpen = true"
        >
          {{ t('settings.btn') }}
        </button>
      </div>
    </header>

    <nav
      class="flex h-9 shrink-0 items-stretch gap-0 border-b border-pve-border bg-[#2e2e2e] px-1"
      aria-label="Main"
    >
      <button
        type="button"
        class="border-b-2 px-4 text-xs font-semibold transition-colors"
        :class="
          mainTab === 'console'
            ? 'border-pve-accent text-white'
            : 'border-transparent text-pve-muted hover:text-pve-text'
        "
        @click="mainTab = 'console'"
      >
        {{ t('nav.console') }}
      </button>
      <button
        type="button"
        class="border-b-2 px-4 text-xs font-semibold transition-colors"
        :class="
          mainTab === 'desktop'
            ? 'border-pve-accent text-white'
            : 'border-transparent text-pve-muted hover:text-pve-text'
        "
        @click="mainTab = 'desktop'"
      >
        {{ t('nav.desktop') }}
      </button>
    </nav>

    <Teleport to="body">
      <div
        v-show="settingsOpen"
        class="fixed inset-0 z-[100] flex justify-end bg-black/50"
        role="presentation"
        @click.self="settingsOpen = false"
      >
        <aside
          class="flex h-full w-full max-w-md flex-col border-l border-pve-border bg-pve-panel shadow-2xl"
          role="dialog"
          :aria-label="t('settings.title')"
          @click.stop
        >
          <div class="flex items-center justify-between border-b border-pve-border bg-pve-header px-3 py-2">
            <span class="text-xs font-semibold uppercase tracking-wide text-pve-text">{{ t('settings.title') }}</span>
            <button
              type="button"
              class="rounded px-2 py-1 text-xs text-pve-muted hover:bg-pve-border hover:text-white"
              @click="settingsOpen = false"
            >
              {{ t('settings.close') }}
            </button>
          </div>
          <div class="min-h-0 flex-1 overflow-y-auto p-4 text-sm">
            <section class="mb-6">
              <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">
                {{ t('settings.langSection') }}
              </h3>
              <label class="mb-1 block text-xs text-pve-muted">{{ t('settings.langLabel') }}</label>
              <select
                class="w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-xs text-pve-text focus:border-pve-accent focus:outline-none"
                :value="locale"
                @change="onLangChange"
              >
                <option value="en">{{ t('settings.lang.en') }}</option>
                <option value="zh">{{ t('settings.lang.zh') }}</option>
                <option value="ko">{{ t('settings.lang.ko') }}</option>
              </select>
            </section>

            <section class="mb-6">
              <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('video.section') }}</h3>
              <label class="mb-1 block text-xs text-pve-muted">{{ t('video.label') }}</label>
              <textarea
                v-model="settingsCameraDraft"
                rows="3"
                class="mb-2 w-full resize-y rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-xs text-pve-text placeholder:text-pve-muted focus:border-pve-accent focus:outline-none"
                :placeholder="t('video.placeholder')"
              />
              <p class="mb-3 text-xs leading-relaxed text-pve-muted">
                {{ t('video.hint') }}
              </p>
              <div class="flex flex-wrap gap-2">
                <button
                  type="button"
                  class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent"
                  @click="saveSettings"
                >
                  {{ t('video.saveApply') }}
                </button>
                <button
                  type="button"
                  class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-muted hover:text-pve-warn"
                  @click="clearStoredCamera"
                >
                  {{ t('video.clearUrl') }}
                </button>
              </div>
            </section>

            <section class="mb-6">
              <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('serial.section') }}</h3>
              <div class="mb-2 flex items-center gap-2">
                <button
                  type="button"
                  class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs font-semibold text-pve-text hover:bg-pve-header"
                  :disabled="serialListLoading"
                  @click="refreshSerialDevices"
                >
                  {{ serialListLoading ? t('serial.scanning') : t('serial.refresh') }}
                </button>
                <span class="font-mono text-[10px] text-pve-muted">OS: {{ serialHostOS || '—' }}</span>
              </div>
              <p v-if="serialHostOS && serialHostOS !== 'linux'" class="mb-3 text-xs text-pve-warn">
                {{ t('serial.nonlinux') }}
              </p>
              <p class="mb-3 text-xs leading-relaxed text-pve-muted">{{ t('serial.hint') }}</p>

              <div
                v-for="role in SERIAL_ROLE_KEYS"
                :key="role"
                class="mb-3"
              >
                <label class="mb-1 block text-xs text-pve-muted">{{ serialRoleTitle(role) }}</label>
                <select
                  v-model="serialRolesDraft[role]"
                  class="w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-[11px] text-pve-text focus:border-pve-accent focus:outline-none"
                >
                  <option value="">{{ t('serial.unassigned') }}</option>
                  <option
                    v-for="d in serialDevices"
                    :key="role + d.path"
                    :value="d.path"
                  >
                    {{ deviceLabel(d) }}
                  </option>
                </select>
              </div>
              <p
                v-if="serialHostOS === 'linux' && !serialListLoading && serialDevices.length === 0"
                class="text-xs text-pve-warn"
              >
                {{ t('serial.emptyList') }}
              </p>
            </section>

            <section class="mb-6">
              <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('conn.section') }}</h3>
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs font-semibold text-pve-text hover:bg-pve-header"
                @click="reconnectWebSocket"
              >
                {{ t('conn.reconnectWs') }}
              </button>
              <p class="mt-2 text-xs text-pve-muted">{{ t('conn.hint') }}</p>
            </section>

            <section class="mb-6">
              <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('ctrl.section') }}</h3>
              <label class="flex cursor-pointer items-center gap-2 text-xs text-pve-text">
                <input v-model="keyboardEnabled" type="checkbox" class="accent-pve-accent" />
                {{ t('ctrl.keyboard') }}
              </label>
              <p class="mt-2 text-xs text-pve-muted">{{ t('ctrl.hint') }}</p>
            </section>

            <section class="mb-6">
              <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('disp.section') }}</h3>
              <label class="mb-1 block text-xs text-pve-muted">{{ t('disp.logBuffer') }}</label>
              <input
                v-model.number="maxLogLines"
                type="number"
                min="50"
                max="5000"
                class="w-full rounded border border-pve-border bg-pve-bg px-2 py-1 font-mono text-xs text-pve-text focus:border-pve-accent focus:outline-none"
              />
            </section>

            <section class="rounded border border-dashed border-pve-border bg-pve-bg/80 p-3">
              <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('ros.section') }}</h3>
              <p class="text-xs leading-relaxed text-pve-muted">
                {{ t('ros.body') }}
              </p>
            </section>
          </div>
        </aside>
      </div>
    </Teleport>

    <div class="flex min-h-0 flex-1 flex-col">
      <template v-if="mainTab === 'console'">
        <div class="flex min-h-0 flex-1 flex-col lg:flex-row">
          <section
            class="flex min-h-[240px] w-full shrink-0 flex-col border-b border-pve-border lg:min-h-0 lg:w-[42%] lg:border-b-0 lg:border-r"
          >
            <div class="pve-panel-title flex items-center justify-between">
              <span>{{ t('video.panelTitle') }}</span>
              <span v-if="!cameraSrc" class="normal-case text-pve-warn">{{ t('video.noUrl') }}</span>
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
                  {{ t('video.emptyHint.before') }}
                  <strong class="text-pve-text">{{ t('video.emptyHint.settings') }}</strong>
                  {{ t('video.emptyHint.after') }}
                </p>
              </div>
            </div>
          </section>

          <HostTopologyGraph :edge-logs="edgeLogs" />
        </div>

        <footer
          class="shrink-0 border-t border-pve-border bg-pve-panel px-4 py-3 shadow-[inset_0_1px_0_#4a4a4a]"
        >
          <div class="mb-2 text-xs font-semibold uppercase tracking-wider text-pve-muted">
            {{ t('op.section') }}
          </div>
          <div class="flex flex-wrap items-center gap-4 text-sm">
            <div class="flex items-center gap-2">
              <span class="pve-kbd">W</span>
              <span class="text-pve-muted">{{ t('op.forward') }}</span>
            </div>
            <div class="flex items-center gap-2">
              <span class="pve-kbd">S</span>
              <span class="text-pve-muted">{{ t('op.reverse') }}</span>
            </div>
            <div class="flex items-center gap-2">
              <span class="pve-kbd">A</span>
              <span class="text-pve-muted">{{ t('op.strafeL') }}</span>
            </div>
            <div class="flex items-center gap-2">
              <span class="pve-kbd">D</span>
              <span class="text-pve-muted">{{ t('op.strafeR') }}</span>
            </div>
            <div class="flex items-center gap-2 border-l border-pve-border pl-4">
              <span class="pve-kbd">Q</span>
              <span class="text-pve-muted">{{ t('op.rotCCW') }}</span>
            </div>
            <div class="flex items-center gap-2">
              <span class="pve-kbd">E</span>
              <span class="text-pve-muted">{{ t('op.rotCW') }}</span>
            </div>
            <div class="ml-auto font-mono text-xs text-pve-accent">
              {{ t('op.active') }} <span class="text-white">{{ lastCmdLabel }}</span>
            </div>
          </div>
        </footer>
      </template>

      <WebDesktopRoot v-else :log-lines="consoleLines" @open-settings="settingsOpen = true" />
    </div>

    <FloatingLogWindow :lines="consoleLines" />
  </div>
</template>
