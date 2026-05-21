<script setup lang="ts">
/*
|--------------------------------------------------------------------------
| 功能工作区组件
|--------------------------------------------------------------------------
| 左侧显示实时图像和键盘说明，右侧整合 SSH 终端与文件管理。
|--------------------------------------------------------------------------
*/
import { ref, watch, nextTick, onMounted } from 'vue'
import { t } from '../../i18n'
import DeskTerminal from '../desktop/DeskTerminal.vue'
import DeskFiles from '../desktop/DeskFiles.vue'

const props = defineProps<{
  cameraSrc?: string
  cameraUseImage: boolean
  lastCmdLabel: string
}>()

const emit = defineEmits<{
  mediaBound: []
  mediaError: []
}>()

const videoRef = ref<HTMLVideoElement | null>(null)

// ==================== 摄像头绑定 ====================
// 作用：video 流需要显式 play；MJPEG 图片流由 img src 自动加载。
// ====================================================
async function bindVideo() {
  await nextTick()
  if (props.cameraUseImage) return
  const el = videoRef.value
  if (!el) return
  if (!props.cameraSrc) {
    el.removeAttribute('src')
    return
  }
  el.src = props.cameraSrc
  el.muted = true
  el.play().then(() => emit('mediaBound')).catch(() => emit('mediaError'))
}

watch(() => [props.cameraSrc, props.cameraUseImage] as const, () => void bindVideo())
onMounted(() => void bindVideo())
</script>

<template>
  <div class="flex min-h-0 flex-1 flex-col">
    <main class="flex min-h-0 flex-1 flex-col lg:flex-row">
      <section class="flex min-h-[320px] shrink-0 flex-col border-b border-pve-border lg:min-h-0 lg:w-1/3 lg:border-b-0 lg:border-r">
        <div class="pve-panel-title flex items-center justify-between">
          <span>{{ t('video.panelTitle') }}</span>
          <span v-if="!cameraSrc" class="normal-case text-pve-warn">{{ t('video.noUrl') }}</span>
        </div>
        <div class="relative min-h-0 flex-1 overflow-hidden bg-black">
          <img
            v-if="cameraSrc && cameraUseImage"
            :src="cameraSrc"
            class="h-full w-full object-fill"
            alt=""
            @load="emit('mediaBound')"
            @error="emit('mediaError')"
          />
          <video v-else-if="cameraSrc" ref="videoRef" class="h-full w-full object-fill" playsinline autoplay muted />
          <div v-else class="flex h-full w-full flex-col items-center justify-center gap-2 p-8 text-center text-pve-muted">
            <div class="h-32 w-full max-w-md border border-dashed border-pve-border bg-pve-panel/50" />
            <p class="max-w-sm font-mono text-xs">
              {{ t('video.emptyHint.before') }}
              <strong class="text-pve-text">{{ t('video.emptyHint.settings') }}</strong>
              {{ t('video.emptyHint.after') }}
            </p>
          </div>
        </div>

        <footer class="shrink-0 border-t border-pve-border bg-pve-panel px-3 py-3 shadow-[inset_0_1px_0_#4a4a4a]">
          <div class="mb-2 text-xs font-semibold uppercase tracking-wider text-pve-muted">{{ t('op.section') }}</div>
          <div class="grid grid-cols-2 gap-2 text-sm">
            <div class="flex items-center gap-2"><span class="pve-kbd">W</span><span class="text-pve-muted">{{ t('op.forward') }}</span></div>
            <div class="flex items-center gap-2"><span class="pve-kbd">S</span><span class="text-pve-muted">{{ t('op.reverse') }}</span></div>
            <div class="flex items-center gap-2"><span class="pve-kbd">A</span><span class="text-pve-muted">{{ t('op.strafeL') }}</span></div>
            <div class="flex items-center gap-2"><span class="pve-kbd">D</span><span class="text-pve-muted">{{ t('op.strafeR') }}</span></div>
            <div class="flex items-center gap-2"><span class="pve-kbd">Q</span><span class="text-pve-muted">{{ t('op.rotCCW') }}</span></div>
            <div class="flex items-center gap-2"><span class="pve-kbd">E</span><span class="text-pve-muted">{{ t('op.rotCW') }}</span></div>
          </div>
          <div class="mt-3 font-mono text-xs text-pve-accent">
            {{ t('op.active') }} <span class="text-white">{{ lastCmdLabel }}</span>
          </div>
        </footer>
      </section>

      <section class="flex min-h-0 flex-1 flex-col overflow-y-auto bg-[#0c0e12] lg:w-2/3">
        <section class="flex min-h-[440px] shrink-0 flex-col p-2 lg:min-h-[62vh]">
          <div class="pve-panel-title flex items-center justify-between rounded-t border border-b-0 border-white/10">
            <span>{{ t('nav.ssh') }}</span>
            <span class="font-mono text-[10px] normal-case text-pve-muted">/ws/shell</span>
          </div>
          <DeskTerminal class="min-h-0 flex-1 rounded-b border border-white/10" />
        </section>

        <section class="flex min-h-[540px] shrink-0 flex-col border-t border-pve-border p-2">
          <div class="pve-panel-title flex items-center justify-between rounded-t border border-b-0 border-white/10">
            <span>{{ t('nav.files') }}</span>
            <span class="font-mono text-[10px] normal-case text-pve-muted">/api/fs/list</span>
          </div>
          <DeskFiles class="min-h-0 flex-1 rounded-b border border-white/10" />
        </section>
      </section>
    </main>
  </div>
</template>
