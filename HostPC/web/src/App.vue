<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed, watch, nextTick } from 'vue'
import { t, locale } from './i18n'
import { emptyEdgeLogMap, isTopologyEdgeId, type TopologyEdgeId } from './topology'
import FloatingLogWindow from './components/FloatingLogWindow.vue'
import HostTopologyGraph from './components/HostTopologyGraph.vue'
import WebDesktopRoot from './components/desktop/WebDesktopRoot.vue'
import SettingsFormBody from './components/SettingsFormBody.vue'

const LS_CAMERA = 'omniroam.camera_url'
const LS_MAXLOG = 'omniroam.console_max_lines'
const LS_KEYBOARD = 'omniroam.keyboard_enabled'
const LS_PWD_DISMISS = 'omniroam.pwd_dismiss'
const SS_GIT_DISMISS = 'omniroam.git_dismiss_remote_sha'

function apiFetch(input: string, init?: RequestInit) {
  return fetch(input, { ...init, credentials: 'include' })
}

const sessionReady = ref(false)
const loggedIn = ref(false)
const authUsername = ref('')
const mustChangePassword = ref(false)
const loginUser = ref('user')
const loginPass = ref('')
const loginError = ref('')
const loginBusy = ref(false)
const pwdModal = ref<'off' | 'nudge' | 'form'>('off')
const newPwd1 = ref('')
const newPwd2 = ref('')
const pwdCurrent = ref('')
const pwdFormError = ref('')
const pwdBusy = ref(false)
const pwdNudgeDismissed = ref(
  typeof sessionStorage !== 'undefined' && sessionStorage.getItem(LS_PWD_DISMISS) === '1',
)

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
const settingsModalOpen = ref(false)
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

type GitUpdatePhase = 'idle' | 'invite' | 'countdown' | 'pulling' | 'done' | 'error'
const gitUpdatePhase = ref<GitUpdatePhase>('idle')
const gitCountdown = ref(10)
const gitBehindInfo = ref<{
  remote_sha: string
  local_sha: string
  branch: string
  remote_url: string
} | null>(null)
const gitPullDetail = ref('')
let gitCountdownTimer: ReturnType<typeof setInterval> | null = null
let gitPollTimer: ReturnType<typeof setInterval> | null = null

function clearGitCountdown() {
  if (gitCountdownTimer) {
    clearInterval(gitCountdownTimer)
    gitCountdownTimer = null
  }
}

async function checkGitRepoUpdate() {
  if (!loggedIn.value) return
  if (gitUpdatePhase.value !== 'idle') return
  try {
    const r = await apiFetch('/api/repo/status')
    if (r.status === 401) return
    if (!r.ok) return
    const j = (await r.json()) as {
      ok?: boolean
      behind?: boolean
      remote_sha?: string
      local_sha?: string
      branch?: string
      remote_url?: string
      fetch_ok?: boolean
      fetch_error?: string
    }
    if (!j.ok || !j.behind || !j.remote_sha) return
    if (typeof sessionStorage !== 'undefined') {
      const dismissed = sessionStorage.getItem(SS_GIT_DISMISS)
      if (dismissed === j.remote_sha) return
    }
    gitBehindInfo.value = {
      remote_sha: j.remote_sha,
      local_sha: typeof j.local_sha === 'string' ? j.local_sha : '',
      branch: typeof j.branch === 'string' ? j.branch : '',
      remote_url: typeof j.remote_url === 'string' ? j.remote_url : '',
    }
    gitUpdatePhase.value = 'invite'
  } catch {
    /* ignore */
  }
}

function dismissGitInvite() {
  if (gitBehindInfo.value?.remote_sha && typeof sessionStorage !== 'undefined') {
    sessionStorage.setItem(SS_GIT_DISMISS, gitBehindInfo.value.remote_sha)
  }
  gitUpdatePhase.value = 'idle'
  gitBehindInfo.value = null
}

function confirmGitInviteProceed() {
  clearGitCountdown()
  gitCountdown.value = 10
  gitUpdatePhase.value = 'countdown'
  gitPullDetail.value = ''
  gitCountdownTimer = setInterval(() => {
    gitCountdown.value -= 1
    if (gitCountdown.value <= 0) {
      clearGitCountdown()
      void runGitPull()
    }
  }, 1000)
}

function cancelGitCountdown() {
  clearGitCountdown()
  gitUpdatePhase.value = 'idle'
}

async function runGitPull() {
  clearGitCountdown()
  gitUpdatePhase.value = 'pulling'
  gitPullDetail.value = ''
  try {
    const r = await apiFetch('/api/repo/pull', { method: 'POST' })
    const j = (await r.json().catch(() => ({}))) as { ok?: boolean; detail?: string; message?: string }
    if (!r.ok) {
      gitPullDetail.value = typeof j.detail === 'string' ? j.detail : t('gitUpdate.pullFail')
      gitUpdatePhase.value = 'error'
      ingestLog(`WARN  ${t('gitUpdate.logPullFail')}`, 'e_http_api')
      return
    }
    gitPullDetail.value = typeof j.message === 'string' ? j.message : ''
    gitUpdatePhase.value = 'done'
    if (typeof sessionStorage !== 'undefined') sessionStorage.removeItem(SS_GIT_DISMISS)
    ingestLog(t('gitUpdate.logPullOk'), 'e_http_api')
  } catch {
    gitUpdatePhase.value = 'error'
    gitPullDetail.value = t('gitUpdate.pullFail')
  }
}

function closeGitUpdateModal() {
  gitUpdatePhase.value = 'idle'
  gitBehindInfo.value = null
  gitPullDetail.value = ''
}

function openSettingsDrawer() {
  settingsModalOpen.value = false
  settingsOpen.value = true
}

function openSettingsModal() {
  settingsOpen.value = false
  settingsModalOpen.value = true
}

function closeAllSettings() {
  settingsOpen.value = false
  settingsModalOpen.value = false
}

function onGitBackdropInviteOnly() {
  if (gitUpdatePhase.value === 'invite') dismissGitInvite()
}

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
  if (!loggedIn.value) return
  if (mainTab.value !== 'console') return
  if (!keyboardEnabled.value) return
  if ((settingsOpen.value || settingsModalOpen.value) && down) return
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

async function checkSession() {
  try {
    const r = await apiFetch('/api/auth/me')
    if (r.ok) {
      const j = (await r.json()) as { username?: string; must_change_password?: boolean }
      loggedIn.value = true
      authUsername.value = typeof j.username === 'string' ? j.username : ''
      mustChangePassword.value = !!j.must_change_password
    } else {
      loggedIn.value = false
      authUsername.value = ''
      mustChangePassword.value = false
    }
  } catch {
    loggedIn.value = false
  } finally {
    sessionReady.value = true
  }
}

async function bootAfterAuth() {
  await hydrateAppliedCameraUrl()
  connectWs()
  await nextTick()
  bindCamera()
  void checkGitRepoUpdate()
  if (gitPollTimer) clearInterval(gitPollTimer)
  gitPollTimer = setInterval(() => void checkGitRepoUpdate(), 5 * 60 * 1000)
}

async function submitLogin() {
  loginError.value = ''
  loginBusy.value = true
  try {
    const r = await apiFetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: loginUser.value.trim(),
        password: loginPass.value,
      }),
    })
    const j = (await r.json().catch(() => ({}))) as { error?: string; must_change_password?: boolean }
    if (!r.ok) {
      if (j.error === 'invalid username or password') {
        loginError.value = t('auth.badCredentials')
      } else {
        loginError.value = typeof j.error === 'string' ? j.error : t('auth.error')
      }
      return
    }
    loggedIn.value = true
    authUsername.value = loginUser.value.trim()
    mustChangePassword.value = !!j.must_change_password
    loginPass.value = ''
    await bootAfterAuth()
    if (mustChangePassword.value && !pwdNudgeDismissed.value) {
      pwdModal.value = 'nudge'
    }
  } catch {
    loginError.value = t('auth.error')
  } finally {
    loginBusy.value = false
  }
}

async function submitLogout() {
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
  wsState.value = 'disconnected'
  try {
    await apiFetch('/api/auth/logout', { method: 'POST' })
  } catch {
    /* ignore */
  }
  loggedIn.value = false
  authUsername.value = ''
  mustChangePassword.value = false
  pwdModal.value = 'off'
  clearGitCountdown()
  if (gitPollTimer) {
    clearInterval(gitPollTimer)
    gitPollTimer = null
  }
  gitUpdatePhase.value = 'idle'
  gitBehindInfo.value = null
}

async function submitChangePassword() {
  pwdFormError.value = ''
  pwdBusy.value = true
  try {
    const body: Record<string, string> = {
      new_password: newPwd1.value,
      new_password_confirm: newPwd2.value,
    }
    if (!mustChangePassword.value) {
      body.current_password = pwdCurrent.value
    }
    const r = await apiFetch('/api/auth/change-password', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    const j = (await r.json().catch(() => ({}))) as { error?: string }
    if (!r.ok) {
      if (j.error === 'passwords do not match') pwdFormError.value = t('auth.passwordMismatch')
      else if (j.error === 'password too short') pwdFormError.value = t('auth.passwordShort')
      else if (j.error === 'current password incorrect')
        pwdFormError.value = t('auth.currentWrong')
      else if (j.error === 'current password required')
        pwdFormError.value = t('auth.currentRequired')
      else pwdFormError.value = typeof j.error === 'string' ? j.error : t('auth.error')
      return
    }
    mustChangePassword.value = false
    pwdModal.value = 'off'
    pwdCurrent.value = ''
    newPwd1.value = ''
    newPwd2.value = ''
    if (typeof sessionStorage !== 'undefined') sessionStorage.removeItem(LS_PWD_DISMISS)
    pwdNudgeDismissed.value = false
  } catch {
    pwdFormError.value = t('auth.error')
  } finally {
    pwdBusy.value = false
  }
}

function dismissPwdNudge() {
  pwdModal.value = 'off'
  pwdNudgeDismissed.value = true
  if (typeof sessionStorage !== 'undefined') sessionStorage.setItem(LS_PWD_DISMISS, '1')
}

function onPwdBackdrop() {
  if (pwdModal.value === 'nudge') dismissPwdNudge()
}

function openPwdFormFromNudge() {
  pwdFormError.value = ''
  pwdCurrent.value = ''
  newPwd1.value = ''
  newPwd2.value = ''
  pwdModal.value = 'form'
}

function openPwdFormVoluntary() {
  pwdFormError.value = ''
  pwdCurrent.value = ''
  newPwd1.value = ''
  newPwd2.value = ''
  pwdModal.value = 'form'
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
      const r = await apiFetch('/api/settings')
      if (r.ok) server = (await r.json()) as { camera_url?: string }
      else if (r.status === 401) loggedIn.value = false
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
    const r = await apiFetch('/api/serial/devices')
    if (r.status === 401) {
      loggedIn.value = false
      return
    }
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
    const r = await apiFetch('/api/settings')
    if (r.status === 401) {
      loggedIn.value = false
      return
    }
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
    const r = await apiFetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ camera_url: url, serial_roles }),
    })
    if (r.status === 401) {
      loggedIn.value = false
      return
    }
    if (!r.ok) ingestLog(t('log.settingsReject'), 'e_http_api')
  } catch {
    ingestLog(t('log.settingsLocalOnly'), 'e_http_api')
  }
  maxLogLines.value = Math.max(50, Math.min(5000, Math.round(maxLogLines.value)))
  localStorage.setItem(LS_MAXLOG, String(maxLogLines.value))
  await nextTick()
  bindCamera()
  closeAllSettings()
}

function clearStoredCamera() {
  settingsCameraDraft.value = ''
  appliedCameraUrl.value = ''
  localStorage.removeItem(LS_CAMERA)
  void saveSettings()
}

watch([settingsOpen, settingsModalOpen], ([drawer, modal]) => {
  if (drawer || modal) {
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
  await checkSession()
  if (loggedIn.value) {
    await bootAfterAuth()
    if (mustChangePassword.value && !pwdNudgeDismissed.value) {
      pwdModal.value = 'nudge'
    }
  }
})

onUnmounted(() => {
  window.removeEventListener('keydown', onWindowKeyDown)
  window.removeEventListener('keyup', onWindowKeyUp)
  if (reconnectTimer) clearTimeout(reconnectTimer)
  clearGitCountdown()
  if (gitPollTimer) {
    clearInterval(gitPollTimer)
    gitPollTimer = null
  }
  wsAllowReconnect = false
  try {
    ws?.close()
  } catch {
    /* ignore */
  }
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
    <div
      v-if="!sessionReady"
      class="flex flex-1 items-center justify-center font-mono text-sm text-pve-muted"
    >
      {{ t('auth.checking') }}
    </div>
    <div
      v-else-if="!loggedIn"
      class="flex flex-1 flex-col items-center justify-center gap-6 p-6"
    >
      <div class="w-full max-w-sm rounded border border-pve-border bg-pve-panel p-6 shadow-xl">
        <h1 class="mb-1 text-center text-lg font-semibold text-white">{{ t('auth.loginTitle') }}</h1>
        <p class="mb-4 text-center text-xs leading-relaxed text-pve-muted">
          {{ t('auth.loginSubtitle') }}
        </p>
        <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.username') }}</label>
        <input
          v-model="loginUser"
          type="text"
          autocomplete="username"
          class="mb-3 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
        />
        <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.password') }}</label>
        <input
          v-model="loginPass"
          type="password"
          autocomplete="current-password"
          class="mb-3 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
          @keydown.enter="submitLogin"
        />
        <p v-if="loginError" class="mb-2 font-mono text-xs text-pve-err">{{ loginError }}</p>
        <button
          type="button"
          class="w-full rounded border border-pve-border bg-pve-header py-2 text-sm font-semibold text-white hover:bg-pve-accent disabled:opacity-50"
          :disabled="loginBusy"
          @click="submitLogin"
        >
          {{ loginBusy ? t('auth.busy') : t('auth.signIn') }}
        </button>
      </div>
    </div>
    <template v-else>
    <header
      class="flex h-9 shrink-0 items-center border-b border-pve-border bg-gradient-to-b from-[#454545] to-[#3a3a3a] px-3 text-sm shadow"
    >
      <span class="font-semibold tracking-tight text-white">OmniRoam</span>
      <span class="mx-2 text-pve-muted">|</span>
      <span class="text-pve-muted">{{ t('app.subtitle') }}</span>
      <span class="ml-6 font-mono text-xs text-pve-accent2">{{ hostDisplay }}</span>
      <div class="ml-auto flex items-center gap-3 font-mono text-xs">
        <span class="text-pve-muted">{{ authUsername }}</span>
        <span :class="statusColor">● {{ wsStateLabel }}</span>
        <button
          type="button"
          class="rounded border border-pve-border bg-pve-panel px-2 py-0.5 text-[11px] font-semibold uppercase tracking-wide text-pve-text shadow hover:bg-pve-header"
          @click="openSettingsDrawer"
        >
          {{ t('settings.btn') }}
        </button>
        <button
          v-if="!mustChangePassword"
          type="button"
          class="rounded border border-pve-border bg-pve-panel px-2 py-0.5 text-[11px] font-semibold uppercase tracking-wide text-pve-text shadow hover:bg-pve-header"
          @click="openPwdFormVoluntary"
        >
          {{ t('auth.changePasswordBtn') }}
        </button>
        <button
          type="button"
          class="rounded border border-pve-border bg-pve-panel px-2 py-0.5 text-[11px] font-semibold uppercase tracking-wide text-pve-warn shadow hover:bg-pve-header"
          @click="submitLogout"
        >
          {{ t('auth.signOut') }}
        </button>
      </div>
    </header>

    <div
      v-if="mustChangePassword && pwdNudgeDismissed"
      class="flex shrink-0 items-center justify-between gap-2 border-b border-amber-600/40 bg-amber-900/25 px-3 py-1.5 text-xs text-amber-200"
    >
      <span>{{ t('auth.bannerNudge') }}</span>
      <button
        type="button"
        class="rounded border border-amber-500/50 px-2 py-0.5 font-semibold text-amber-100 hover:bg-amber-800/40"
        @click="
          pwdFormError = '';
          pwdCurrent = '';
          newPwd1 = '';
          newPwd2 = '';
          pwdModal = 'form'
        "
      >
        {{ t('auth.pwdNudgeChange') }}
      </button>
    </div>

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
        @click.self="closeAllSettings"
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
              @click="closeAllSettings"
            >
              {{ t('settings.close') }}
            </button>
          </div>
          <SettingsFormBody
            v-model:camera-url="settingsCameraDraft"
            v-model:serial-roles-draft="serialRolesDraft"
            v-model:max-log-lines="maxLogLines"
            v-model:keyboard-enabled="keyboardEnabled"
            :locale-val="locale"
            :serial-devices="serialDevices"
            :serial-list-loading="serialListLoading"
            :host-os="serialHostOS"
            @save="saveSettings"
            @clear-camera="clearStoredCamera"
            @refresh-serial="refreshSerialDevices"
            @reconnect-ws="reconnectWebSocket"
          />
        </aside>
      </div>
    </Teleport>

    <Teleport to="body">
      <div
        v-show="settingsModalOpen"
        class="fixed inset-0 z-[140] flex items-center justify-center bg-black/55 p-4"
        role="presentation"
        @click.self="closeAllSettings"
      >
        <div
          class="flex max-h-[min(90vh,720px)] w-full max-w-lg flex-col overflow-hidden rounded border border-pve-border bg-pve-panel shadow-2xl"
          role="dialog"
          :aria-label="t('settings.titleModal')"
          @click.stop
        >
          <div class="flex shrink-0 items-center justify-between border-b border-pve-border bg-pve-header px-3 py-2">
            <span class="text-xs font-semibold uppercase tracking-wide text-pve-text">{{ t('settings.titleModal') }}</span>
            <button
              type="button"
              class="rounded px-2 py-1 text-xs text-pve-muted hover:bg-pve-border hover:text-white"
              @click="closeAllSettings"
            >
              {{ t('settings.close') }}
            </button>
          </div>
          <SettingsFormBody
            v-model:camera-url="settingsCameraDraft"
            v-model:serial-roles-draft="serialRolesDraft"
            v-model:max-log-lines="maxLogLines"
            v-model:keyboard-enabled="keyboardEnabled"
            :locale-val="locale"
            :serial-devices="serialDevices"
            :serial-list-loading="serialListLoading"
            :host-os="serialHostOS"
            @save="saveSettings"
            @clear-camera="clearStoredCamera"
            @refresh-serial="refreshSerialDevices"
            @reconnect-ws="reconnectWebSocket"
          />
        </div>
      </div>
    </Teleport>

    <Teleport to="body">
      <div
        v-if="gitUpdatePhase !== 'idle'"
        class="fixed inset-0 z-[180] flex items-center justify-center bg-black/60 p-4"
        role="presentation"
        @click.self="onGitBackdropInviteOnly"
      >
        <div
          class="w-full max-w-md rounded border border-pve-border bg-pve-panel p-5 shadow-2xl"
          role="alertdialog"
          @click.stop
        >
          <template v-if="gitUpdatePhase === 'invite' && gitBehindInfo">
            <h2 class="mb-2 text-sm font-semibold text-white">{{ t('gitUpdate.title') }}</h2>
            <p class="mb-3 text-xs leading-relaxed text-pve-muted">{{ t('gitUpdate.body') }}</p>
            <p class="mb-1 font-mono text-[10px] text-pve-muted">{{ t('gitUpdate.branch') }} {{ gitBehindInfo.branch }}</p>
            <p class="mb-3 break-all font-mono text-[10px] text-pve-muted">{{ gitBehindInfo.remote_url }}</p>
            <div class="mt-4 flex flex-wrap justify-end gap-2">
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header"
                @click="dismissGitInvite"
              >
                {{ t('gitUpdate.later') }}
              </button>
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent"
                @click="confirmGitInviteProceed"
              >
                {{ t('gitUpdate.proceed') }}
              </button>
            </div>
          </template>

          <template v-else-if="gitUpdatePhase === 'countdown'">
            <h2 class="mb-2 text-sm font-semibold text-white">{{ t('gitUpdate.countdownTitle') }}</h2>
            <p class="mb-4 text-xs leading-relaxed text-pve-muted">
              {{ t('gitUpdate.countdownLead') }}
              <strong class="font-mono text-white">{{ gitCountdown }}</strong>
              {{ t('gitUpdate.countdownSuffix') }}
            </p>
            <div class="flex flex-wrap justify-end gap-2">
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header"
                @click="cancelGitCountdown"
              >
                {{ t('gitUpdate.cancel') }}
              </button>
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent"
                @click="clearGitCountdown(); void runGitPull()"
              >
                {{ t('gitUpdate.pullNow') }}
              </button>
            </div>
          </template>

          <template v-else-if="gitUpdatePhase === 'pulling'">
            <h2 class="mb-2 text-sm font-semibold text-white">{{ t('gitUpdate.pulling') }}</h2>
            <p class="text-xs text-pve-muted">{{ t('gitUpdate.pullingHint') }}</p>
          </template>

          <template v-else-if="gitUpdatePhase === 'done'">
            <h2 class="mb-2 text-sm font-semibold text-pve-ok">{{ t('gitUpdate.doneTitle') }}</h2>
            <p class="mb-3 text-xs leading-relaxed text-pve-muted">{{ t('gitUpdate.doneBody') }}</p>
            <pre
              v-if="gitPullDetail"
              class="mb-3 max-h-32 overflow-auto rounded border border-pve-border bg-pve-bg p-2 font-mono text-[10px] text-pve-text"
            >{{ gitPullDetail }}</pre>
            <div class="flex justify-end">
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent"
                @click="closeGitUpdateModal"
              >
                {{ t('gitUpdate.close') }}
              </button>
            </div>
          </template>

          <template v-else-if="gitUpdatePhase === 'error'">
            <h2 class="mb-2 text-sm font-semibold text-pve-err">{{ t('gitUpdate.errorTitle') }}</h2>
            <pre
              class="mb-3 max-h-40 overflow-auto whitespace-pre-wrap break-all rounded border border-pve-border bg-pve-bg p-2 font-mono text-[10px] text-pve-warn"
            >{{ gitPullDetail }}</pre>
            <div class="flex flex-wrap justify-end gap-2">
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header"
                @click="closeGitUpdateModal"
              >
                {{ t('gitUpdate.close') }}
              </button>
            </div>
          </template>
        </div>
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

          <HostTopologyGraph :edge-logs="edgeLogs" @open-settings-modal="openSettingsModal" />
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

      <WebDesktopRoot v-else :log-lines="consoleLines" @open-settings="openSettingsDrawer" />
    </div>

    <FloatingLogWindow :lines="consoleLines" />
    </template>

    <Teleport to="body">
      <div
        v-if="loggedIn && pwdModal !== 'off'"
        class="fixed inset-0 z-[200] flex items-center justify-center bg-black/60 p-4"
        role="presentation"
        @click.self="onPwdBackdrop"
      >
        <div
          class="w-full max-w-md rounded border border-pve-border bg-pve-panel p-5 shadow-2xl"
          role="dialog"
          @click.stop
        >
          <template v-if="pwdModal === 'nudge'">
            <h2 class="mb-2 text-sm font-semibold text-white">{{ t('auth.pwdNudgeTitle') }}</h2>
            <p class="mb-4 text-xs leading-relaxed text-pve-muted">{{ t('auth.pwdNudgeBody') }}</p>
            <div class="flex flex-wrap justify-end gap-2">
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header"
                @click="dismissPwdNudge"
              >
                {{ t('auth.pwdNudgeLater') }}
              </button>
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent"
                @click="openPwdFormFromNudge"
              >
                {{ t('auth.pwdNudgeChange') }}
              </button>
            </div>
          </template>
          <template v-else-if="pwdModal === 'form'">
            <h2 class="mb-2 text-sm font-semibold text-white">{{ t('auth.pwdChangeTitle') }}</h2>
            <template v-if="!mustChangePassword">
              <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.currentPassword') }}</label>
              <input
                v-model="pwdCurrent"
                type="password"
                autocomplete="current-password"
                class="mb-2 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
              />
            </template>
            <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.newPassword') }}</label>
            <input
              v-model="newPwd1"
              type="password"
              autocomplete="new-password"
              class="mb-2 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
            />
            <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.confirmPassword') }}</label>
            <input
              v-model="newPwd2"
              type="password"
              autocomplete="new-password"
              class="mb-2 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
              @keydown.enter="submitChangePassword"
            />
            <p v-if="pwdFormError" class="mb-2 font-mono text-xs text-pve-err">{{ pwdFormError }}</p>
            <div class="flex flex-wrap justify-end gap-2">
              <button
                v-if="mustChangePassword"
                type="button"
                class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header"
                @click="pwdModal = pwdNudgeDismissed ? 'off' : 'nudge'"
              >
                {{ t('auth.back') }}
              </button>
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent disabled:opacity-50"
                :disabled="pwdBusy"
                @click="submitChangePassword"
              >
                {{ pwdBusy ? t('auth.busy') : t('auth.submit') }}
              </button>
            </div>
          </template>
        </div>
      </div>
    </Teleport>
  </div>
</template>
