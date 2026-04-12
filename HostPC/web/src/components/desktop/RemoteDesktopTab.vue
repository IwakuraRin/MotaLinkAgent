<script setup lang="ts">
// noVNC 始终走同源 /ws/vnc（HostPC 转发到本机 TCP VNC），无需在网页里填 WebSocket URL
//
import { ref, computed, watch, onUnmounted, nextTick } from 'vue'
import { t } from '../../i18n'
// eslint-disable-next-line @typescript-eslint/no-explicit-any
import RFBMod from '@novnc/novnc/lib/rfb.js'
import DeskTerminal from './DeskTerminal.vue'
import DeskLogs from './DeskLogs.vue'
import DeskFiles from './DeskFiles.vue'
import DeskAbout from './DeskAbout.vue'

const RFB = (RFBMod as any).default ?? RFBMod

const props = defineProps<{
  logLines: string[]
  novncPassword: string
  desktopActive: boolean
}>()

const emit = defineEmits<{
  openSettings: []
}>()

const rfbWsUrl = computed(() => {
  const p = location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${p}//${location.host}/ws/vnc`
})

const rfbHostRef = ref<HTMLElement | null>(null)
let rfb: InstanceType<typeof RFB> | null = null

const status = ref<'idle' | 'connecting' | 'connected' | 'error'>('idle')
const statusDetail = ref('')
const toolsOpen = ref(false)
const toolOverlay = ref<'off' | 'logs' | 'files' | 'about'>('off')
const terminalCollapsed = ref(false)

function statusLabel() {
  if (status.value === 'connecting') return t('novnc.status.connecting')
  if (status.value === 'connected') return t('novnc.status.connected')
  if (status.value === 'error') return t('novnc.status.error')
  return t('novnc.status.idle')
}

function clearRfbTarget() {
  const el = rfbHostRef.value
  if (el) el.replaceChildren()
}

function teardownRfb() {
  if (rfb) {
    try {
      rfb.disconnect()
    } catch {
      /* ignore */
    }
    rfb = null
  }
  clearRfbTarget()
}

function connectNovnc() {
  teardownRfb()
  const url = rfbWsUrl.value
  const target = rfbHostRef.value
  if (!target) return

  status.value = 'connecting'
  statusDetail.value = ''

  try {
    rfb = new RFB(target, url, {
      credentials: { password: props.novncPassword || '' },
    })
  } catch (e) {
    status.value = 'error'
    statusDetail.value = e instanceof Error ? e.message : String(e)
    return
  }

  rfb.scaleViewport = true
  rfb.resizeSession = true
  rfb.background = '#1a1a1a'

  rfb.addEventListener('connect', () => {
    status.value = 'connected'
    statusDetail.value = ''
  })

  rfb.addEventListener('disconnect', (ev: Event) => {
    const ce = ev as CustomEvent<{ clean?: boolean }>
    const clean = ce.detail?.clean
    rfb = null
    clearRfbTarget()
    if (status.value === 'connecting') {
      status.value = 'error'
      statusDetail.value = t('novnc.disconnectAborted')
    } else if (!clean) {
      status.value = 'error'
      statusDetail.value = t('novnc.disconnectUnclean')
    } else {
      status.value = 'idle'
      statusDetail.value = ''
    }
  })

  rfb.addEventListener('credentialsrequired', () => {
    try {
      rfb?.sendCredentials({ password: props.novncPassword || '' })
    } catch {
      /* ignore */
    }
  })

  rfb.addEventListener('securityfailure', (ev: Event) => {
    const ce = ev as CustomEvent<{ status?: number; reason?: string }>
    status.value = 'error'
    const r = ce.detail?.reason
    statusDetail.value = r ? `${t('novnc.securityFailure')}: ${r}` : t('novnc.securityFailure')
    teardownRfb()
  })
}

watch(
  () => props.desktopActive,
  (active) => {
    if (active) {
      void nextTick(() => connectNovnc())
    } else {
      disconnectNovnc()
    }
  },
  { immediate: true },
)

watch(
  () => props.novncPassword,
  () => {
    if (props.desktopActive) void nextTick(() => connectNovnc())
  },
)

function disconnectNovnc() {
  teardownRfb()
  status.value = 'idle'
  statusDetail.value = ''
}

function openTool(kind: 'logs' | 'files' | 'about') {
  toolsOpen.value = false
  toolOverlay.value = kind
}

function closeToolOverlay() {
  toolOverlay.value = 'off'
}

function toggleTools() {
  toolsOpen.value = !toolsOpen.value
}

onUnmounted(() => {
  teardownRfb()
})
</script>

<template>
  <div class="flex min-h-0 min-w-0 flex-1 flex-col bg-[#2e2e2e]">
    <!-- PVE 风格顶栏 -->
    <div
      class="relative z-[50] flex h-9 shrink-0 items-center gap-2 border-b border-pve-border bg-[#2e2e2e] px-2"
      @pointerdown.stop
    >
      <span class="text-xs font-semibold uppercase tracking-wide text-pve-text">{{ t('novnc.toolbarTitle') }}</span>
      <div class="mx-1 h-5 w-px bg-pve-border" />
      <button
        type="button"
        class="rounded border border-pve-border bg-pve-header px-2 py-0.5 text-[11px] font-semibold text-white hover:bg-pve-accent disabled:opacity-40"
        :disabled="status === 'connecting'"
        @click="connectNovnc"
      >
        {{ t('novnc.connect') }}
      </button>
      <button
        type="button"
        class="rounded border border-pve-border bg-pve-bg px-2 py-0.5 text-[11px] text-pve-text hover:bg-pve-header disabled:opacity-40"
        :disabled="status !== 'connected' && status !== 'connecting'"
        @click="disconnectNovnc"
      >
        {{ t('novnc.disconnect') }}
      </button>
      <div class="relative" @pointerdown.stop>
        <button
          type="button"
          class="rounded border border-pve-border bg-pve-bg px-2 py-0.5 text-[11px] text-pve-text hover:bg-pve-header"
          @click="toggleTools"
        >
          {{ t('desktop.tools') }}
        </button>
        <div
          v-if="toolsOpen"
          class="absolute left-0 top-full z-[60] mt-0.5 w-44 overflow-hidden rounded border border-pve-border bg-pve-panel py-1 shadow-xl"
          @pointerdown.stop
        >
          <button
            type="button"
            class="block w-full px-3 py-1.5 text-left text-xs text-pve-text hover:bg-pve-header"
            @click="openTool('logs')"
          >
            {{ t('desktop.tools.logs') }}
          </button>
          <button
            type="button"
            class="block w-full px-3 py-1.5 text-left text-xs text-pve-text hover:bg-pve-header"
            @click="openTool('files')"
          >
            {{ t('desktop.tools.files') }}
          </button>
          <button
            type="button"
            class="block w-full px-3 py-1.5 text-left text-xs text-pve-text hover:bg-pve-header"
            @click="openTool('about')"
          >
            {{ t('desktop.tools.about') }}
          </button>
          <div class="my-1 border-t border-pve-border" />
          <button
            type="button"
            class="block w-full px-3 py-1.5 text-left text-xs text-pve-text hover:bg-pve-header"
            @click="
              toolsOpen = false;
              emit('openSettings')
            "
          >
            {{ t('desktop.app.settings') }}
          </button>
        </div>
      </div>
      <span class="ml-auto max-w-[55%] truncate font-mono text-[10px] text-pve-muted" :title="rfbWsUrl">
        {{ statusLabel() }}
        <span v-if="statusDetail" class="text-pve-warn"> — {{ statusDetail }}</span>
      </span>
    </div>

    <div class="flex min-h-0 min-w-0 flex-1" @pointerdown="toolsOpen = false">
      <!-- noVNC -->
      <div class="relative flex min-h-0 min-w-0 flex-1 flex-col bg-black">
        <div
          v-if="status === 'error'"
          class="pointer-events-none absolute inset-0 z-[1] flex items-center justify-center p-6 text-center"
        >
          <p class="max-w-md text-xs leading-relaxed text-pve-muted">
            {{ t('novnc.emptyHint') }}
          </p>
        </div>
        <div ref="rfbHostRef" class="min-h-0 flex-1 touch-none overflow-hidden" />
      </div>

      <!-- 右侧终端 -->
      <aside
        v-show="!terminalCollapsed"
        class="flex min-h-0 w-[min(100%,420px)] min-w-[280px] shrink-0 flex-col border-l border-pve-border bg-[#0c0e12]"
        aria-label="Host terminal"
      >
        <div class="flex h-8 shrink-0 items-center justify-between border-b border-pve-border bg-pve-header px-2">
          <span class="text-[11px] font-semibold uppercase tracking-wide text-pve-text">{{
            t('desktop.terminalPanel')
          }}</span>
          <button
            type="button"
            class="rounded px-1.5 py-0.5 font-mono text-[10px] text-pve-muted hover:bg-pve-border hover:text-white"
            :title="t('desktop.terminalCollapse')"
            @click="terminalCollapsed = true"
          >
            ››
          </button>
        </div>
        <div class="min-h-0 flex-1 p-1">
          <DeskTerminal class="h-full min-h-[120px] rounded border border-white/10" />
        </div>
      </aside>
      <button
        v-show="terminalCollapsed"
        type="button"
        class="flex w-9 shrink-0 flex-col items-center justify-center gap-1 border-l border-pve-border bg-pve-panel py-2 text-[10px] leading-tight text-pve-muted hover:bg-pve-header hover:text-white"
        :title="t('desktop.app.terminal')"
        @click="terminalCollapsed = false"
      >
        <span class="-rotate-90 whitespace-nowrap">{{ t('desktop.app.terminal') }}</span>
        <span class="font-mono text-xs">‹</span>
      </button>
    </div>

    <!-- 工具浮层 -->
    <Teleport to="body">
      <div
        v-if="toolOverlay !== 'off'"
        class="fixed inset-0 z-[130] flex items-center justify-center bg-black/55 p-4"
        role="presentation"
        @click.self="closeToolOverlay"
      >
        <div
          class="flex max-h-[85vh] w-full max-w-3xl flex-col overflow-hidden rounded border border-pve-border bg-pve-panel shadow-2xl"
          role="dialog"
          @click.stop
        >
          <div class="flex items-center justify-between border-b border-pve-border px-3 py-2">
            <span class="text-xs font-semibold text-white">
              {{
                toolOverlay === 'logs'
                  ? t('desktop.app.logs')
                  : toolOverlay === 'files'
                    ? t('desktop.app.files')
                    : t('desktop.app.about')
              }}
            </span>
            <button
              type="button"
              class="rounded px-2 py-1 text-xs text-pve-muted hover:bg-pve-border hover:text-white"
              @click="closeToolOverlay"
            >
              {{ t('settings.close') }}
            </button>
          </div>
          <div class="min-h-0 flex-1 overflow-auto p-3">
            <DeskLogs v-if="toolOverlay === 'logs'" :lines="logLines" />
            <DeskFiles v-else-if="toolOverlay === 'files'" />
            <DeskAbout v-else />
          </div>
        </div>
      </div>
    </Teleport>
  </div>
</template>
