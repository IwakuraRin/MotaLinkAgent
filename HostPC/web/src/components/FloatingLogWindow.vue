<script setup lang="ts">
import { ref, watch, nextTick, onUnmounted } from 'vue'
import { t } from '../i18n'

const props = defineProps<{
  lines: string[]
}>()

const maximized = ref(false)
const logBodyRef = ref<HTMLElement | null>(null)

const useLeftTop = ref(false)
const pos = ref({ left: 0, top: 0 })
const dragOffset = ref({ x: 0, y: 0 })

const WIN_W = 320
const WIN_H = 220

/** Only true while primary button is held after a valid title-bar pointerdown */
let dragActive = false

function toggleMaximize() {
  maximized.value = !maximized.value
  if (!maximized.value) {
    useLeftTop.value = false
  }
}

function endLogDrag() {
  if (!dragActive) return
  dragActive = false
  document.removeEventListener('pointermove', onDocPointerMove, true)
  document.removeEventListener('pointerup', endLogDrag, true)
  document.removeEventListener('pointercancel', endLogDrag, true)
}

function onDocPointerMove(e: PointerEvent) {
  if (!dragActive || maximized.value) return
  if ((e.buttons & 1) === 0) {
    endLogDrag()
    return
  }
  pos.value = {
    left: e.clientX - dragOffset.value.x,
    top: e.clientY - dragOffset.value.y,
  }
}

function onTitlePointerDown(e: PointerEvent) {
  if (maximized.value) return
  if (e.button !== 0) return
  if ((e.target as HTMLElement).closest('.float-log-zoom')) return

  const el = (e.currentTarget as HTMLElement).closest('.floating-log-shell') as HTMLElement
  if (!el) return

  const r = el.getBoundingClientRect()
  useLeftTop.value = true
  pos.value = { left: r.left, top: r.top }
  dragOffset.value = { x: e.clientX - r.left, y: e.clientY - r.top }

  endLogDrag()
  dragActive = true
  document.addEventListener('pointermove', onDocPointerMove, true)
  document.addEventListener('pointerup', endLogDrag, true)
  document.addEventListener('pointercancel', endLogDrag, true)
  e.preventDefault()
}

watch(
  () => props.lines.length,
  async () => {
    await nextTick()
    const el = logBodyRef.value
    if (el) el.scrollTop = el.scrollHeight
  },
)

onUnmounted(() => {
  endLogDrag()
})
</script>

<template>
  <div
    class="floating-log-shell fixed z-[220] flex flex-col overflow-hidden rounded-md border border-pve-border bg-[#1a1a1a] shadow-2xl"
    :class="[
      maximized ? 'inset-3' : '',
      !maximized && !useLeftTop ? 'bottom-4 right-4' : '',
    ]"
    :style="
      maximized
        ? {}
        : useLeftTop
          ? { left: pos.left + 'px', top: pos.top + 'px', width: WIN_W + 'px', height: WIN_H + 'px' }
          : { width: WIN_W + 'px', height: WIN_H + 'px' }
    "
  >
    <div
      class="flex h-8 shrink-0 cursor-default select-none items-center gap-2 border-b border-pve-border bg-gradient-to-b from-[#3d3d3d] to-[#353535] px-2 active:cursor-grabbing"
      @pointerdown="onTitlePointerDown"
    >
      <button
        type="button"
        class="float-log-zoom h-3 w-3 shrink-0 cursor-pointer rounded-full border border-[#b8860b]/60 bg-[#f6d365] shadow hover:brightness-110"
        :title="maximized ? t('floatLog.shrink') : t('floatLog.expand')"
        aria-label="Toggle maximize"
        @click.stop="toggleMaximize"
      />
      <span class="select-none text-xs font-semibold text-pve-text">{{ t('floatLog.title') }}</span>
    </div>
    <pre
      ref="logBodyRef"
      class="m-0 min-h-0 flex-1 overflow-auto bg-[#141414] p-2 font-mono text-[10px] leading-snug text-[#b8e0b8]"
      >{{ lines.join('\n') }}</pre
    >
  </div>
</template>
