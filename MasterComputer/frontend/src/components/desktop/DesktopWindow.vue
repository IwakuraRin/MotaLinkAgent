<script setup lang="ts">
import { ref, onUnmounted } from 'vue'
import type { DesktopWin } from '../../desktop/types'

const props = defineProps<{
  model: DesktopWin
}>()

const emit = defineEmits<{
  focus: []
  close: []
  minimize: []
  toggleMaximize: []
  move: [x: number, y: number]
}>()

const rootRef = ref<HTMLElement | null>(null)
let dragActive = false
let startClient = { x: 0, y: 0 }
let startPos = { x: 0, y: 0 }

function endDrag() {
  if (!dragActive) return
  dragActive = false
  document.removeEventListener('pointermove', onDocMove, true)
  document.removeEventListener('pointerup', endDrag, true)
  document.removeEventListener('pointercancel', endDrag, true)
}

function onDocMove(e: PointerEvent) {
  if (!dragActive) return
  if ((e.buttons & 1) === 0) {
    endDrag()
    return
  }
  const dx = e.clientX - startClient.x
  const dy = e.clientY - startClient.y
  emit('move', startPos.x + dx, startPos.y + dy)
}

function onTitlePointerDown(e: PointerEvent) {
  if (e.button !== 0) return
  if ((e.target as HTMLElement).closest('.traffic-light')) return
  if (props.model.maximized) return
  emit('focus')
  dragActive = true
  startClient = { x: e.clientX, y: e.clientY }
  startPos = { x: props.model.x, y: props.model.y }
  document.addEventListener('pointermove', onDocMove, true)
  document.addEventListener('pointerup', endDrag, true)
  document.addEventListener('pointercancel', endDrag, true)
  e.preventDefault()
}

onUnmounted(() => {
  endDrag()
})

function onRootPointerDown() {
  emit('focus')
}
</script>

<template>
  <div
    ref="rootRef"
    class="absolute flex flex-col overflow-hidden rounded-lg border border-white/20 bg-[#1e2430] shadow-2xl"
    @pointerdown.stop="onRootPointerDown"
    :style="{
      left: model.x + 'px',
      top: model.y + 'px',
      width: model.w + 'px',
      height: model.h + 'px',
      zIndex: model.z,
    }"
  >
    <div
      class="flex h-9 shrink-0 cursor-default items-center gap-2 border-b border-white/10 bg-gradient-to-b from-[#3a4556] to-[#2c3544] px-2 select-none"
      @pointerdown="onTitlePointerDown"
    >
      <div class="flex shrink-0 items-center gap-1.5 pl-0.5">
        <button
          type="button"
          class="traffic-light h-3 w-3 rounded-full border border-black/20 bg-[#ff5f57] shadow-inner hover:brightness-110"
          aria-label="Close"
          title="Close"
          @click.stop="emit('close')"
          @pointerdown.stop
        />
        <button
          type="button"
          class="traffic-light h-3 w-3 rounded-full border border-black/20 bg-[#febc2e] shadow-inner hover:brightness-110"
          aria-label="Minimize"
          title="Minimize"
          @click.stop="emit('minimize')"
          @pointerdown.stop
        />
        <button
          type="button"
          class="traffic-light h-3 w-3 rounded-full border border-black/20 bg-[#28c840] shadow-inner hover:brightness-110"
          aria-label="Maximize"
          title="Maximize"
          @click.stop="emit('toggleMaximize')"
          @pointerdown.stop
        />
      </div>
      <span class="min-w-0 flex-1 truncate text-center text-xs font-medium text-white/90">{{ model.title }}</span>
      <span class="w-[52px] shrink-0" aria-hidden="true" />
    </div>
    <div class="min-h-0 flex-1 overflow-hidden bg-[#12161c] p-1">
      <slot />
    </div>
  </div>
</template>
