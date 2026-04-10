<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { t } from '../../i18n'
import type { DesktopWin, DesktopAppKind } from '../../desktop/types'
import DesktopWindow from './DesktopWindow.vue'
import DeskTerminal from './DeskTerminal.vue'
import DeskLogs from './DeskLogs.vue'
import DeskAbout from './DeskAbout.vue'

defineProps<{
  logLines: string[]
}>()

const emit = defineEmits<{
  openSettings: []
}>()

const workspaceRef = ref<HTMLElement | null>(null)
const windows = ref<DesktopWin[]>([])
const startOpen = ref(false)
let nextId = 1
let zCounter = 20

const timeStr = ref('')
let clockId: ReturnType<typeof setInterval> | null = null
let workspaceResizeRo: ResizeObserver | null = null

function tickClock() {
  timeStr.value = new Date().toLocaleTimeString(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  })
}

const visibleWindows = computed(() => windows.value.filter((w) => !w.minimized))

function titleFor(kind: DesktopAppKind): string {
  if (kind === 'terminal') return t('desktop.app.terminal')
  if (kind === 'logs') return t('desktop.app.logs')
  return t('desktop.app.about')
}

function focusWin(id: number) {
  zCounter += 1
  const w = windows.value.find((x) => x.id === id)
  if (w) w.z = zCounter
}

function applyMaximizedToWorkspace() {
  const ws = workspaceRef.value
  if (!ws) return
  const pad = 4
  for (const w of windows.value) {
    if (!w.maximized || w.minimized) continue
    w.x = pad
    w.y = pad
    w.w = Math.max(240, ws.clientWidth - pad * 2)
    w.h = Math.max(160, ws.clientHeight - pad * 2)
  }
}

function openApp(kind: DesktopAppKind | 'settings') {
  startOpen.value = false
  if (kind === 'settings') {
    emit('openSettings')
    return
  }
  const existing = windows.value.find((w) => w.kind === kind && !w.minimized)
  if (existing) {
    focusWin(existing.id)
    return
  }
  const min = windows.value.find((w) => w.kind === kind && w.minimized)
  if (min) {
    min.minimized = false
    focusWin(min.id)
    return
  }

  zCounter += 1
  const n = windows.value.length
  const w = kind === 'terminal' ? 560 : kind === 'logs' ? 520 : 440
  const h = kind === 'terminal' ? 340 : kind === 'logs' ? 360 : 280
  windows.value.push({
    id: nextId++,
    kind,
    title: titleFor(kind),
    x: 48 + (n % 4) * 28,
    y: 40 + (n % 4) * 24,
    w,
    h,
    z: zCounter,
    minimized: false,
    maximized: false,
  })
}

function closeWin(id: number) {
  windows.value = windows.value.filter((w) => w.id !== id)
}

function minimizeWin(id: number) {
  const w = windows.value.find((x) => x.id === id)
  if (w) w.minimized = true
}

function toggleMaximizeWin(id: number) {
  const ws = workspaceRef.value
  const w = windows.value.find((o) => o.id === id)
  if (!ws || !w) return
  if (w.maximized) {
    w.maximized = false
    const r = w.restoreBounds
    delete w.restoreBounds
    if (r) {
      w.x = r.x
      w.y = r.y
      w.w = r.w
      w.h = r.h
    }
  } else {
    w.restoreBounds = { x: w.x, y: w.y, w: w.w, h: w.h }
    w.maximized = true
    const pad = 4
    w.x = pad
    w.y = pad
    w.w = Math.max(240, ws.clientWidth - pad * 2)
    w.h = Math.max(160, ws.clientHeight - pad * 2)
  }
}

function taskbarClick(w: DesktopWin) {
  if (w.minimized) {
    w.minimized = false
  }
  focusWin(w.id)
}

function onWinMove(id: number, x: number, y: number) {
  const ws = workspaceRef.value
  const w = windows.value.find((o) => o.id === id)
  if (!ws || !w || w.maximized) return
  const maxX = Math.max(8, ws.clientWidth - w.w - 8)
  const maxY = Math.max(8, ws.clientHeight - w.h - 8)
  w.x = Math.max(4, Math.min(maxX, x))
  w.y = Math.max(4, Math.min(maxY, y))
}

function toggleStart() {
  startOpen.value = !startOpen.value
}

function onWorkspacePointerDown() {
  startOpen.value = false
}

onMounted(() => {
  tickClock()
  clockId = setInterval(tickClock, 1000)
  void nextTick(() => {
    const el = workspaceRef.value
    if (!el || typeof ResizeObserver === 'undefined') return
    workspaceResizeRo = new ResizeObserver(() => {
      applyMaximizedToWorkspace()
    })
    workspaceResizeRo.observe(el)
  })
})

onUnmounted(() => {
  if (clockId) clearInterval(clockId)
  workspaceResizeRo?.disconnect()
  workspaceResizeRo = null
})
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col bg-[#05080c]">
    <!-- Top taskbar -->
    <div
      class="relative z-[100] flex h-11 shrink-0 items-center gap-1 border-b border-black/50 bg-[#1c2430]/98 px-2 backdrop-blur"
      @pointerdown.stop
    >
      <button
        type="button"
        class="rounded border border-white/20 bg-[#3a7ab8] px-3 py-1 text-xs font-bold text-white shadow hover:bg-[#4588c8]"
        @click="toggleStart"
      >
        {{ t('desktop.startButton') }}
      </button>
      <div class="mx-1 h-6 w-px bg-white/15" />
      <button
        v-for="w in windows"
        :key="'tb-' + w.id"
        type="button"
        class="max-w-[140px] truncate rounded px-2 py-1 text-left text-[11px] text-[#c8d4e0] hover:bg-white/10"
        :class="w.minimized ? 'opacity-60' : ''"
        @click="taskbarClick(w)"
      >
        {{ w.title }}
      </button>
      <span class="ml-auto font-mono tabular-nums text-xs text-[#a8b8c8]">{{ timeStr }}</span>
    </div>

    <div
      ref="workspaceRef"
      class="web-desktop-workspace relative min-h-0 flex-1 overflow-hidden"
      @pointerdown="onWorkspacePointerDown"
    >
      <div
        class="pointer-events-none absolute inset-0 bg-gradient-to-br from-[#1a3050] via-[#243a50] to-[#0c1828]"
      />
      <div
        class="pointer-events-none absolute inset-0 opacity-40"
        style="
          background-image: radial-gradient(ellipse 80% 50% at 50% 0%, rgba(120, 170, 255, 0.12), transparent),
            radial-gradient(circle at 15% 80%, rgba(255, 255, 255, 0.06) 0%, transparent 35%);
        "
      />

      <!-- Desktop icons -->
      <div class="absolute left-4 top-6 z-[5] flex flex-col gap-6" @pointerdown.stop>
        <button
          type="button"
          class="flex w-[76px] flex-col items-center rounded p-2 text-center hover:bg-white/10"
          @dblclick="openApp('terminal')"
        >
          <span class="text-3xl leading-none" aria-hidden="true">⌨</span>
          <span class="mt-1 text-[11px] leading-tight text-white drop-shadow">{{ t('desktop.app.terminal') }}</span>
        </button>
        <button
          type="button"
          class="flex w-[76px] flex-col items-center rounded p-2 text-center hover:bg-white/10"
          @dblclick="openApp('logs')"
        >
          <span class="text-3xl leading-none" aria-hidden="true">📋</span>
          <span class="mt-1 text-[11px] leading-tight text-white drop-shadow">{{ t('desktop.app.logs') }}</span>
        </button>
        <button
          type="button"
          class="flex w-[76px] flex-col items-center rounded p-2 text-center hover:bg-white/10"
          @dblclick="openApp('about')"
        >
          <span class="text-3xl leading-none" aria-hidden="true">ℹ</span>
          <span class="mt-1 text-[11px] leading-tight text-white drop-shadow">{{ t('desktop.app.about') }}</span>
        </button>
        <button
          type="button"
          class="flex w-[76px] flex-col items-center rounded p-2 text-center hover:bg-white/10"
          @dblclick="emit('openSettings')"
        >
          <span class="text-3xl leading-none" aria-hidden="true">⚙</span>
          <span class="mt-1 text-[11px] leading-tight text-white drop-shadow">{{ t('desktop.app.settings') }}</span>
        </button>
      </div>

      <p class="pointer-events-none absolute bottom-4 right-4 z-[5] max-w-xs text-right text-[10px] text-white/35">
        {{ t('desktop.iconHint') }}
      </p>

      <DesktopWindow
        v-for="w in visibleWindows"
        :key="w.id"
        :model="w"
        @focus="focusWin(w.id)"
        @close="closeWin(w.id)"
        @minimize="minimizeWin(w.id)"
        @toggle-maximize="toggleMaximizeWin(w.id)"
        @move="(x, y) => onWinMove(w.id, x, y)"
      >
        <DeskTerminal v-if="w.kind === 'terminal'" />
        <DeskLogs v-else-if="w.kind === 'logs'" :lines="logLines" />
        <DeskAbout v-else />
      </DesktopWindow>

      <!-- Start menu drops below top taskbar -->
      <div
        v-if="startOpen"
        class="absolute left-2 top-12 z-[200] w-56 overflow-hidden rounded-lg border border-white/15 bg-[#2a3344]/98 py-2 shadow-2xl backdrop-blur"
        @pointerdown.stop
      >
        <div class="border-b border-white/10 px-3 py-2 text-xs font-semibold text-white/90">
          {{ t('desktop.startMenu') }}
        </div>
        <button
          type="button"
          class="block w-full px-3 py-2 text-left text-xs text-[#d0dce8] hover:bg-white/10"
          @click="openApp('terminal')"
        >
          {{ t('desktop.app.terminal') }}
        </button>
        <button
          type="button"
          class="block w-full px-3 py-2 text-left text-xs text-[#d0dce8] hover:bg-white/10"
          @click="openApp('logs')"
        >
          {{ t('desktop.app.logs') }}
        </button>
        <button
          type="button"
          class="block w-full px-3 py-2 text-left text-xs text-[#d0dce8] hover:bg-white/10"
          @click="openApp('about')"
        >
          {{ t('desktop.app.about') }}
        </button>
        <button
          type="button"
          class="block w-full px-3 py-2 text-left text-xs text-[#d0dce8] hover:bg-white/10"
          @click="openApp('settings')"
        >
          {{ t('desktop.app.settings') }}
        </button>
      </div>
    </div>
  </div>
</template>
