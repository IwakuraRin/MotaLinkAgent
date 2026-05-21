<script setup lang="ts">
// 展示代码结构：
//   · 鉴权：登录/登出/改密、会话 apiFetch
//   · 自更新：轮询 /api/updates/status、弹窗与倒计时部署
//   · 左侧菜单：功能页 / 设置页
//   · WebSocket：按键遥控、连接状态维护
//   · 设置页：语言、摄像头 URL、串口绑定等
//   · 模板：功能区、设置区、改密/更新弹窗
//
import { ref, onMounted, onUnmounted, computed, watch } from 'vue'
import { t, locale, setLocale, type Locale } from './i18n'
import AppLogin from './components/app/AppLogin.vue'
import AppHeader from './components/app/AppHeader.vue'
import PasswordNudgeBanner from './components/app/PasswordNudgeBanner.vue'
import SideMenu from './components/app/SideMenu.vue'
import FeatureWorkspace from './components/app/FeatureWorkspace.vue'
import SettingsView from './components/app/SettingsView.vue'
import PasswordDialog from './components/app/PasswordDialog.vue'
import UpdateDialog from './components/app/UpdateDialog.vue'
import { SERIAL_ROLE_KEYS, type MenuKey, type SerialDev, type SerialRoleKey, type UpdateStatusPayload } from './appTypes'

const LS_CAMERA = 'omniroam.camera_url'
const LS_KEYBOARD = 'omniroam.keyboard_enabled'
const LS_PWD_DISMISS = 'omniroam.pwd_dismiss'
const LS_UPDATE_DISMISS = 'omniroam.update.dismiss_remote_sha'

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

const updateStatus = ref<UpdateStatusPayload | null>(null)
const updateModal = ref<'off' | 'prompt' | 'countdown' | 'deploying'>('off')
const updateCountdown = ref(10)
const updateDeployOutput = ref('')
const updateDeployBusy = ref(false)
const updateCheckBusy = ref(false)
const updateCheckMessage = ref('')
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

async function checkUpdateFromSettings() {
  if (!loggedIn.value || updateCheckBusy.value) return
  updateCheckBusy.value = true
  updateCheckMessage.value = ''
  try {
    const r = await apiFetch('/api/updates/status')
    if (r.status === 401) {
      loggedIn.value = false
      return
    }
    const st = (await r.json().catch(() => ({}))) as UpdateStatusPayload
    updateStatus.value = st
    if (!r.ok) {
      updateCheckMessage.value = `HTTP ${r.status}`
    } else if (!st.enabled) {
      updateCheckMessage.value = st.reason || t('update.notConfigured')
    } else if (st.update_available) {
      updateModal.value = 'prompt'
    } else {
      updateCheckMessage.value = t('update.upToDate')
    }
  } catch (e) {
    updateCheckMessage.value = String(e)
  } finally {
    updateCheckBusy.value = false
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

//--------//
// 模块：控制台 — 串口列表、摄像头 URL、键盘控制
type SerialDev = { path: string; target: string; kind: string }

const SERIAL_ROLE_KEYS = ['atmega_uart', 'aux_serial'] as const
type SerialRoleKey = (typeof SERIAL_ROLE_KEYS)[number]

const wsState = ref<'disconnected' | 'connecting' | 'open' | 'error'>('disconnected')
const keysHeld = ref<Record<string, boolean>>({})
const lastCmd = ref('')

const activeMenu = ref<MenuKey>('function')
const settingsOpen = computed(() => activeMenu.value === 'settings')
const settingsCameraDraft = ref('')
const appliedCameraUrl = ref('')
const keyboardEnabled = ref(true)

const serialDevices = ref<SerialDev[]>([])
const serialListLoading = ref(false)
const serialHostOS = ref('')
const serialRolesDraft = ref<Record<SerialRoleKey, string>>({
  atmega_uart: '',
  aux_serial: '',
})

const envCamera = (import.meta.env.VITE_CAMERA_URL as string | undefined)?.trim() || ''
const autoCamera = (() => {
  if (typeof window === 'undefined') return ''
  const host = window.location.hostname
  if (!host) return ''
  return `http://${host}:8081/stream?topic=/obstacle_detector/debug`
})()
const cameraSrc = computed(() => {
  const u = appliedCameraUrl.value.trim()
  if (u) return u
  if (envCamera) return envCamera
  if (autoCamera) return autoCamera
  return undefined
})

function urlLooksLikeMjpeg(u: string): boolean {
  const x = u.toLowerCase()
  if (x.includes('.m3u8')) return false
  return (
    x.includes('mjpeg') ||
    x.includes('.mjpg') ||
    x.includes('mpjpeg') ||
    x.includes('/mjpeg') ||
    x.includes('mjpg') ||
    x.includes('/stream?topic=')
  )
}

const cameraUseImage = computed(() => {
  const src = cameraSrc.value
  if (!src) return false
  return urlLooksLikeMjpeg(src)
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
// 模块：运行事件 — 保留内部调试入口，不再在前端界面显示日志。
function ingestLog(line: string) {
  if (import.meta.env.DEV) console.debug('[AmseokBot]', line)
}

function sendKey(key: string, down: boolean) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'key', key, down }))
  }
}

function onKeyEv(e: KeyboardEvent, down: boolean) {
  if (!loggedIn.value) return
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
    ingestLog(t('log.wsOpen'))
  }
  ws.onclose = () => {
    wsState.value = 'disconnected'
    if (!wsAllowReconnect) return
    ingestLog(t('log.wsClosed'))
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
        ingestLog(j.line)
      } else if (j.type === 'ack' && typeof j.msg === 'string') {
        ingestLog(`ACK   ${j.msg}`)
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
  ingestLog(t('log.wsManualReconnect'))
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
// 模块：摄像头 — 从设置/API hydrate 外部流 URL
async function hydrateAppliedCameraUrl() {
  type CamSettings = {
    camera_url?: string
  }
  let server: CamSettings | null = null
  async function getServer(): Promise<CamSettings | null> {
    if (server) return server
    try {
      const r = await apiFetch('/api/settings')
      if (r.ok) server = (await r.json()) as CamSettings
      else if (r.status === 401) loggedIn.value = false
    } catch {
      /* dev without backend */
    }
    return server
  }

  const lsCam = localStorage.getItem(LS_CAMERA)
  const isStaleRawCameraUrl = (url: string) => url.includes('/usb_cam/image_raw')
  if (lsCam !== null && !isStaleRawCameraUrl(lsCam)) {
    appliedCameraUrl.value = lsCam
  } else {
    const j = await getServer()
    const serverCameraUrl = j && typeof j.camera_url === 'string' ? j.camera_url : ''
    appliedCameraUrl.value = serverCameraUrl || envCamera || autoCamera
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
      serialRolesDraft.value.atmega_uart = sr.atmega_uart ?? sr.esp32_uart ?? ''
      serialRolesDraft.value.aux_serial = sr.aux_serial ?? ''
    }
  } catch {
    /* ignore */
  }
  await refreshSerialDevices()
}

function onLangChange(v: Locale) {
  if (v === 'en' || v === 'zh' || v === 'ko') setLocale(v)
}

function setSerialRole(role: SerialRoleKey, value: string) {
  serialRolesDraft.value[role] = value
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
      body: JSON.stringify({
        camera_url: url,
        serial_roles,
      }),
    })
    if (r.status === 401) {
      loggedIn.value = false
      return
    }
    if (!r.ok) ingestLog(t('log.settingsReject'))
  } catch {
    ingestLog(t('log.settingsLocalOnly'))
  }
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


watch(keyboardEnabled, (v) => {
  localStorage.setItem(LS_KEYBOARD, v ? '1' : '0')
})

//--------//
// 模块：生命周期 — 挂载时恢复本地选项、键盘监听、会话与 WS
onMounted(async () => {
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
  <div class="relative flex h-full min-h-[600px] flex-col bg-pve-bg font-ui text-pve-text" tabindex="0">
    <!-- -------- 模块：会话检查中 -------- -->
    <div v-if="!sessionReady" class="flex flex-1 items-center justify-center font-mono text-sm text-pve-muted">
      {{ t('auth.checking') }}
    </div>

    <AppLogin
      v-else-if="!loggedIn"
      v-model:login-user="loginUser"
      v-model:login-pass="loginPass"
      :login-error="loginError"
      :login-busy="loginBusy"
      @submit="submitLogin"
    />

    <template v-else>
      <AppHeader
        :host-display="hostDisplay"
        :auth-username="authUsername"
        :status-color="statusColor"
        :ws-state-label="wsStateLabel"
        :must-change-password="mustChangePassword"
        @change-password="openPwdFormVoluntary"
        @logout="submitLogout"
      />

      <PasswordNudgeBanner
        v-if="mustChangePassword && pwdNudgeDismissed"
        @open="openPwdFormVoluntary"
      />

      <div class="flex min-h-0 flex-1">
        <SideMenu v-model:active-menu="activeMenu" />

        <FeatureWorkspace
          v-show="activeMenu === 'function'"
          :camera-src="cameraSrc"
          :camera-use-image="cameraUseImage"
          :last-cmd-label="lastCmdLabel"
          @media-bound="ingestLog(t('log.videoBound'))"
          @media-error="ingestLog(t('log.camFail'))"
        />

        <SettingsView
          v-show="activeMenu === 'settings'"
          v-model:camera-draft="settingsCameraDraft"
          v-model:keyboard-enabled="keyboardEnabled"
          :locale="locale"
          :serial-role-keys="SERIAL_ROLE_KEYS"
          :serial-roles-draft="serialRolesDraft"
          :serial-devices="serialDevices"
          :serial-list-loading="serialListLoading"
          :serial-host-o-s="serialHostOS"
          :update-status="updateStatus"
          :update-check-busy="updateCheckBusy"
          :update-check-message="updateCheckMessage"
          :update-shas-line="updateShasLine"
          @update-serial-role="setSerialRole"
          @lang-change="onLangChange"
          @save-settings="saveSettings"
          @clear-stored-camera="clearStoredCamera"
          @refresh-serial-devices="refreshSerialDevices"
          @reconnect-web-socket="reconnectWebSocket"
          @check-update="checkUpdateFromSettings"
        />
      </div>

      <PasswordDialog
        v-model:pwd-current="pwdCurrent"
        v-model:new-pwd1="newPwd1"
        v-model:new-pwd2="newPwd2"
        :logged-in="loggedIn"
        :modal="pwdModal"
        :must-change-password="mustChangePassword"
        :pwd-nudge-dismissed="pwdNudgeDismissed"
        :pwd-form-error="pwdFormError"
        :pwd-busy="pwdBusy"
        @backdrop="onPwdBackdrop"
        @dismiss-nudge="dismissPwdNudge"
        @open-form-from-nudge="openPwdFormFromNudge"
        @submit="submitChangePassword"
        @back="pwdModal = pwdNudgeDismissed ? 'off' : 'nudge'"
      />

      <UpdateDialog
        :logged-in="loggedIn"
        :modal="updateModal"
        :update-status="updateStatus"
        :update-shas-line="updateShasLine"
        :update-countdown="updateCountdown"
        :update-countdown-wait-text="updateCountdownWaitText"
        :update-deploy-output="updateDeployOutput"
        :update-deploy-busy="updateDeployBusy"
        @backdrop="onUpdateBackdrop"
        @later="laterUpdatePrompt"
        @begin-countdown="beginUpdateCountdown"
        @cancel="cancelUpdateFlow"
        @deploy="runDeployUpdate"
        @close="closeDeployModal"
      />
    </template>
  </div>
</template>
