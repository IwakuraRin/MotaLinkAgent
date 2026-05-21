<script setup lang="ts">
/*
|--------------------------------------------------------------------------
| 自更新对话框组件
|--------------------------------------------------------------------------
| 展示更新提示、倒计时确认和部署输出；更新流程由父组件控制。
|--------------------------------------------------------------------------
*/
import { t } from '../../i18n'
import type { UpdateStatusPayload } from '../../appTypes'

defineProps<{
  loggedIn: boolean
  modal: 'off' | 'prompt' | 'countdown' | 'deploying'
  updateStatus: UpdateStatusPayload | null
  updateShasLine: string
  updateCountdown: number
  updateCountdownWaitText: string
  updateDeployOutput: string
  updateDeployBusy: boolean
}>()

const emit = defineEmits<{
  backdrop: []
  later: []
  beginCountdown: []
  cancel: []
  deploy: []
  close: []
}>()
</script>

<template>
  <Teleport to="body">
    <div
      v-if="loggedIn && modal !== 'off'"
      class="fixed inset-0 z-[210] flex items-center justify-center bg-black/65 p-4"
      role="presentation"
      @click.self="emit('backdrop')"
    >
      <div class="max-h-[85vh] w-full max-w-lg overflow-y-auto rounded border border-pve-border bg-pve-panel p-5 shadow-2xl" role="dialog" aria-modal="true" @click.stop>
        <template v-if="modal === 'prompt'">
          <h2 class="mb-2 text-sm font-semibold text-white">{{ t('update.title') }}</h2>
          <p class="mb-2 text-xs leading-relaxed text-pve-muted">{{ t('update.available') }}</p>
          <p class="mb-3 font-mono text-[11px] text-pve-accent2">{{ updateShasLine }}</p>
          <h3 class="mb-1 text-[11px] font-semibold uppercase tracking-wide text-pve-muted">{{ t('update.changelog') }}</h3>
          <pre v-if="updateStatus?.changelog && updateStatus.changelog.trim()" class="mb-3 max-h-40 overflow-y-auto whitespace-pre-wrap rounded border border-pve-border bg-pve-bg p-2 font-mono text-[11px] text-pve-text">{{ updateStatus.changelog }}</pre>
          <p v-else class="mb-3 font-mono text-[11px] text-pve-muted">{{ t('update.noChangelog') }}</p>
          <p v-if="updateStatus?.changelog_error" class="mb-2 font-mono text-[11px] text-pve-warn">
            {{ t('update.changelogFetchErr') }} {{ updateStatus.changelog_error }}
          </p>
          <p v-if="updateStatus?.git_error" class="mb-3 font-mono text-[11px] text-pve-warn">
            {{ t('update.gitErr') }} {{ updateStatus.git_error }}
          </p>
          <div class="flex flex-wrap justify-end gap-2">
            <button type="button" class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header" @click="emit('later')">
              {{ t('update.later') }}
            </button>
            <button type="button" class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent" @click="emit('beginCountdown')">
              {{ t('update.confirm') }}
            </button>
          </div>
        </template>

        <template v-else-if="modal === 'countdown'">
          <h2 class="mb-2 text-sm font-semibold text-white">{{ t('update.countdownTitle') }}</h2>
          <p class="mb-2 text-xs leading-relaxed text-pve-muted">{{ t('update.countdownBody') }}</p>
          <p class="mb-1 text-sm font-semibold text-amber-100/95">{{ t('update.sureQuestion') }}</p>
          <p class="mb-4 font-mono text-xs text-pve-muted">{{ updateCountdownWaitText }}</p>
          <div class="flex flex-wrap justify-end gap-2">
            <button type="button" class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header" @click="emit('cancel')">
              {{ t('update.cancel') }}
            </button>
            <button
              type="button"
              class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent disabled:opacity-40"
              :disabled="updateCountdown > 0"
              @click="emit('deploy')"
            >
              {{ t('update.startDeploy') }}
            </button>
          </div>
        </template>

        <template v-else-if="modal === 'deploying'">
          <h2 class="mb-2 text-sm font-semibold text-white">{{ t('update.title') }}</h2>
          <p v-if="updateDeployBusy" class="mb-2 text-xs text-pve-muted">{{ t('update.deploying') }}</p>
          <pre class="mb-3 max-h-64 overflow-y-auto whitespace-pre-wrap rounded border border-pve-border bg-black/40 p-2 font-mono text-[10px] text-pve-text">{{ updateDeployOutput }}</pre>
          <button v-if="!updateDeployBusy" type="button" class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent" @click="emit('close')">
            {{ t('settings.close') }}
          </button>
        </template>
      </div>
    </div>
  </Teleport>
</template>
