<script setup lang="ts">
// 展示代码结构：
//   · 鉴权：登录/登出/改密、会话 apiFetch
//   · 自更新：轮询 /api/updates/status、弹窗与倒计时部署
//   · 主 Tab：控制台（视频+拓扑+键盘）/ 桌面 WebDesktopRoot
//   · WebSocket：日志、按键遥控、边沿日志 ingestLog
//   · 设置抽屉：语言、摄像头 URL、串口绑定、日志条数等
//   · 模板：Teleport 设置/改密/更新弹窗、FloatingLogWindow
//
import { ref, onMounted, onUnmounted, computed, watch, nextTick } from 'vue'
import { t, locale, setLocale, type Locale } from './i18n'
import { emptyEdgeLogMap, isTopologyEdgeId, type TopologyEdgeId } from './topology'
import FloatingLogWindow from './components/FloatingLogWindow.vue'
import HostTopologyGraph from './components/HostTopologyGraph.vue'
import WebDesktopRoot from './components/desktop/WebDesktopRoot.vue'

const LS_CAMERA = 'omniroam.camera_url'
const LS_MAXLOG = 'omniroam.console_max_lines'
const LS_KEYBOARD = 'omniroam.keyboard_enabled'
const LS_PWD_DISMISS = 'omniroam.pwd_dismiss'
const LS_UPDATE_DISMISS = 'omniroam.update.dismiss_remote_sha'
const UPDATE_POLL_MS = 10 * 60 * 1000

function apiFetch(input: string, init?: RequestInit) {
  return fetch(input, { ...init, credentials: 'include' })
}

//--------//
// 模块：鉴权状态 — 登录表单、会话检查、改密弹窗
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

type UpdateStatusPayload = {
  enabled: boolean
  update_available?: boolean
  local_sha?: string
  remote_sha?: string
  branch?: string
  changelog?: string
  changelog_ok?: boolean
  changelog_error?: string
  git_error?: string
  reason?: string
}

const updateStatus = ref<UpdateStatusPayload | null>(null)
const updateModal = ref<'off' | 'prompt' | 'countdown' | 'deploying'>('off')
const updateCountdown = ref(10)
const updateDeployOutput = ref('')
const updateDeployBusy = ref(false)
let updatePollHandle: ReturnType<typeof setInterval> | null = null
let updateCountdownHandle: ReturnType<typeof setInterval> | null = null

//--------//
// 模块：HostPC 自更新 — 状态轮询、弹窗流程、调用 /api/updates/apply
function shortGitSha(s: string | undefined) {
  if (!s) return '—'
  return s.length > 7 ? s.slice(0, 7) : s
}

const updateShasLine = computed(() => {
  const st = updateStatus.value
  if (!st) return ''
  return t('update.shas')
    .replace('{{local}}', shortGitSha(st.local_sha))
    .replace('{{remote}}', shortGitSha(st.remote_sha))
    .replace('{{branch}}', st.branch || 'main')
})

function dismissUpdateForRemoteSha() {
  const sha = updateStatus.value?.remote_sha
  if (sha && typeof sessionStorage !== 'undefined') {
    sessionStorage.setItem(LS_UPDATE_DISMISS, sha)
  }
}

function maybeOpenUpdateModal(st: UpdateStatusPayload) {
  if (updateModal.value !== 'off') return
  if (!st.enabled || !st.update_available || !st.remote_sha) return
  if (typeof sessionStorage !== 'undefined') {
    if (sessionStorage.getItem(LS_UPDATE_DISMISS) === st.remote_sha) return
  }
  updateModal.value = 'prompt'
}

async function pollUpdateStatus() {
  if (!loggedIn.value) return
  try {
    const r = await apiFetch('/api/updates/status')
    if (r.status === 401) {
      loggedIn.value = false
      return
    }
    if (!r.ok) return
    const st = (await r.json()) as UpdateStatusPayload
    updateStatus.value = st
    maybeOpenUpdateModal(st)
  } catch {
    /* offline / dev */
  }
}

function startUpdatePolling() {
  stopUpdatePolling()
  void pollUpdateStatus()
  updatePollHandle = setInterval(() => void pollUpdateStatus(), UPDATE_POLL_MS)
}

function stopUpdatePolling() {
  if (updatePollHandle) {
    clearInterval(updatePollHandle)
    updatePollHandle = null
  }
}

function clearUpdateCountdown() {
  if (updateCountdownHandle) {
    clearInterval(updateCountdownHandle)
    updateCountdownHandle = null
  }
}

function laterUpdatePrompt() {
  dismissUpdateForRemoteSha()
  updateModal.value = 'off'
}

function beginUpdateCountdown() {
  updateModal.value = 'countdown'
  clearUpdateCountdown()
  updateCountdown.value = 10
  updateCountdownHandle = setInterval(() => {
    updateCountdown.value--
    if (updateCountdown.value <= 0) {
      updateCountdown.value = 0
      clearUpdateCountdown()
    }
  }, 1000)
}

function cancelUpdateFlow() {
  clearUpdateCountdown()
  updateModal.value = 'off'
  dismissUpdateForRemoteSha()
}

function closeDeployModal() {
  updateModal.value = 'off'
  updateDeployOutput.value = ''
}

const updateCountdownWaitText = computed(() =>
  t('update.countdownWait').replace('{{n}}', String(updateCountdown.value)),
)

async function runDeployUpdate() {
  updateDeployBusy.value = true
  updateDeployOutput.value = ''
  updateModal.value = 'deploying'
  try {
    const r = await apiFetch('/api/updates/apply', { method: 'POST' })
    const j = (await r.json().catch(() => ({}))) as {
      ok?: boolean
      output?: string
      error?: string
      exit_code?: number
    }
    const out = typeof j.output === 'string' ? j.output : ''
    if (r.status === 409) {
      updateDeployOutput.value = t('update.busy')
    } else if (r.status === 400) {
      const err = typeof j.error === 'string' ? j.error : ''
      updateDeployOutput.value = `${err}\n${out}\n\n${t('update.deployFail')}`
    } else if (!r.ok) {
      updateDeployOutput.value = `HTTP ${r.status}\n\n${t('update.deployFail')}`
    } else if (j.ok) {
      updateDeployOutput.value = `${out}\n\n${t('update.deployOk')}`
      dismissUpdateForRemoteSha()
    } else {
      updateDeployOutput.value = `${out}\n\n${t('update.deployFail')}`
    }
  } catch (e) {
    updateDeployOutput.value = `${String(e)}\n\n${t('update.deployFail')}`
  } finally {
    updateDeployBusy.value = false
  }
}

function onUpdateBackdrop() {
  if (updateModal.value === 'prompt') {
    dismissUpdateForRemoteSha()
    updateModal.value = 'off'
  } else if (updateModal.value === 'countdown') {
    cancelUpdateFlow()
  }
}

const mainTab = ref<'console' | 'desktop'>(
  typeof sessionStorage !== 'undefined' && sessionStorage.getItem('omniroam.main_tab') === 'desktop'
    ? 'desktop'
    : 'console',
)

watch(mainTab, (v) => {
  if (typeof sessionStorage !== 'undefined') sessionStorage.setItem('omniroam.main_tab', v)
})

//--------//
// 模块：控制台 — 串口列表、日志缓冲、摄像头 URL、拓扑边日志
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

//--------//
// 模块：日志行 — 主控制台缓冲 + 按拓扑边分栏
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

//--------//
// 模块：WebSocket — 连接、重连、消息解析为日志
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

//--------//
// 模块：鉴权 API — checkSession、登录登出改密、登录后引导
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
  startUpdatePolling()
  await nextTick()
  bindCamera()
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
  stopUpdatePolling()
  clearUpdateCountdown()
  updateModal.value = 'off'
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

//--------//
// 模块：改密弹窗 — 打开/关闭表单
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

//--------//
// 模块：摄像头 — 绑定 video 元素、从设置/API hydrate URL
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

//--------//
// 模块：串口与设置面板 — 枚举设备、加载 serial_roles、保存 settings
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

//--------//
// 模块：生命周期 — 挂载时恢复本地选项、键盘监听、会话与 WS
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
  stopUpdatePolling()
  clearUpdateCountdown()
  wsAllowReconnect = false
  try {
    ws?.close()
  } catch {
    /* ignore */
  }
})

//--------//
// 模块：UI 派生 — WebSocket 状态颜色
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
  <!-- -------- 根布局：会话门控 → 登录页 或 主界面 -------- -->
  <div
    class="relative flex h-full min-h-[600px] flex-col bg-pve-bg font-ui text-pve-text"
    tabindex="0"
  >
    <!-- -------- 模块：会话检查中 -------- -->
    <div
      v-if="!sessionReady"
      class="flex flex-1 items-center justify-center font-mono text-sm text-pve-muted"
    >
      {{ t('auth.checking') }}
    </div>
    <!-- -------- 模块：未登录 — 登录表单 -------- -->
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
    <!-- -------- 模块：顶栏 — 标题、WS 状态、设置/改密/登出 -------- -->
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
          @click="settingsOpen = true"
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

    <!-- -------- 模块：改密提醒条 -------- -->
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

    <!-- -------- 模块：主 Tab — 控制台 / 桌面 -------- -->
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

    <!-- -------- 模块：设置侧栏（语言/视频/串口/连接/控制/显示） -------- -->
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

    <!-- -------- 模块：主内容区 -------- -->
    <div class="flex min-h-0 flex-1 flex-col">
      <template v-if="mainTab === 'console'">
        <!-- -------- 子模块：控制台 — 视频 + 拓扑图 -------- -->
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

        <!-- -------- 子模块：底盘操作说明（WASD/QE） -------- -->
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

      <!-- -------- 子模块：仿桌面（终端/文件等） -------- -->
      <WebDesktopRoot v-else :log-lines="consoleLines" @open-settings="settingsOpen = true" />
    </div>

    <!-- -------- 模块：可拖拽悬浮日志窗口 -------- -->
    <FloatingLogWindow :lines="consoleLines" />
    </template>

    <!-- -------- 模块：改密对话框 -------- -->
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

    <!-- -------- 模块：自更新提示与部署输出 -------- -->
    <Teleport to="body">
      <div
        v-if="loggedIn && updateModal !== 'off'"
        class="fixed inset-0 z-[210] flex items-center justify-center bg-black/65 p-4"
        role="presentation"
        @click.self="onUpdateBackdrop"
      >
        <div
          class="max-h-[85vh] w-full max-w-lg overflow-y-auto rounded border border-pve-border bg-pve-panel p-5 shadow-2xl"
          role="dialog"
          aria-modal="true"
          @click.stop
        >
          <template v-if="updateModal === 'prompt'">
            <h2 class="mb-2 text-sm font-semibold text-white">{{ t('update.title') }}</h2>
            <p class="mb-2 text-xs leading-relaxed text-pve-muted">{{ t('update.available') }}</p>
            <p class="mb-3 font-mono text-[11px] text-pve-accent2">{{ updateShasLine }}</p>
            <h3 class="mb-1 text-[11px] font-semibold uppercase tracking-wide text-pve-muted">
              {{ t('update.changelog') }}
            </h3>
            <pre
              v-if="updateStatus?.changelog && updateStatus.changelog.trim()"
              class="mb-3 max-h-40 overflow-y-auto whitespace-pre-wrap rounded border border-pve-border bg-pve-bg p-2 font-mono text-[11px] text-pve-text"
              >{{ updateStatus.changelog }}</pre>
            <p v-else class="mb-3 font-mono text-[11px] text-pve-muted">{{ t('update.noChangelog') }}</p>
            <p v-if="updateStatus?.changelog_error" class="mb-2 font-mono text-[11px] text-pve-warn">
              {{ t('update.changelogFetchErr') }} {{ updateStatus.changelog_error }}
            </p>
            <p v-if="updateStatus?.git_error" class="mb-3 font-mono text-[11px] text-pve-warn">
              {{ t('update.gitErr') }} {{ updateStatus.git_error }}
            </p>
            <div class="flex flex-wrap justify-end gap-2">
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header"
                @click="
                  dismissUpdateForRemoteSha();
                  updateModal = 'off'
                "
              >
                {{ t('update.later') }}
              </button>
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent"
                @click="beginUpdateCountdown"
              >
                {{ t('update.confirm') }}
              </button>
            </div>
          </template>

          <template v-else-if="updateModal === 'countdown'">
            <h2 class="mb-2 text-sm font-semibold text-white">{{ t('update.countdownTitle') }}</h2>
            <p class="mb-2 text-xs leading-relaxed text-pve-muted">{{ t('update.countdownBody') }}</p>
            <p class="mb-1 text-sm font-semibold text-amber-100/95">{{ t('update.sureQuestion') }}</p>
            <p class="mb-4 font-mono text-xs text-pve-muted">{{ updateCountdownWaitText }}</p>
            <div class="flex flex-wrap justify-end gap-2">
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header"
                @click="cancelUpdateFlow"
              >
                {{ t('update.cancel') }}
              </button>
              <button
                type="button"
                class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent disabled:opacity-40"
                :disabled="updateCountdown > 0"
                @click="runDeployUpdate"
              >
                {{ t('update.startDeploy') }}
              </button>
            </div>
          </template>

          <template v-else-if="updateModal === 'deploying'">
            <h2 class="mb-2 text-sm font-semibold text-white">{{ t('update.title') }}</h2>
            <p v-if="updateDeployBusy" class="mb-2 text-xs text-pve-muted">{{ t('update.deploying') }}</p>
            <pre
              class="mb-3 max-h-64 overflow-y-auto whitespace-pre-wrap rounded border border-pve-border bg-black/40 p-2 font-mono text-[10px] text-pve-text"
              >{{ updateDeployOutput }}</pre
            >
            <button
              v-if="!updateDeployBusy"
              type="button"
              class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent"
              @click="closeDeployModal"
            >
              {{ t('settings.close') }}
            </button>
          </template>
        </div>
      </div>
    </Teleport>
  </div>
</template>
