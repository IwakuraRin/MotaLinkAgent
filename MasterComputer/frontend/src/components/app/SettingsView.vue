<script setup lang="ts">
/*
|--------------------------------------------------------------------------
| 设置页面组件
|--------------------------------------------------------------------------
| 集中展示语言、摄像头、串口、连接、更新、键盘控制和 ROS 说明配置项。
|--------------------------------------------------------------------------
*/
import { t, type Locale } from '../../i18n'
import type { SerialDev, SerialRoleKey, UpdateStatusPayload } from '../../appTypes'

const props = defineProps<{
  locale: Locale
  cameraDraft: string
  keyboardEnabled: boolean
  serialRoleKeys: readonly SerialRoleKey[]
  serialRolesDraft: Record<SerialRoleKey, string>
  serialDevices: SerialDev[]
  serialListLoading: boolean
  serialHostOS: string
  updateStatus: UpdateStatusPayload | null
  updateCheckBusy: boolean
  updateCheckMessage: string
  updateShasLine: string
}>()

const emit = defineEmits<{
  'update:cameraDraft': [value: string]
  'update:keyboardEnabled': [value: boolean]
  updateSerialRole: [role: SerialRoleKey, value: string]
  langChange: [value: Locale]
  saveSettings: []
  clearStoredCamera: []
  refreshSerialDevices: []
  reconnectWebSocket: []
  checkUpdate: []
}>()

function deviceLabel(d: SerialDev): string {
  if (d.target && d.target !== d.path) return `${d.path} → ${d.target}`
  return d.path
}

function serialRoleTitle(role: SerialRoleKey): string {
  if (role === 'atmega_uart') return t('serial.role.atmega_uart')
  if (role === 'aux_serial') return t('serial.role.aux_serial')
  return role
}

function emitLangChange(e: Event) {
  const v = (e.target as HTMLSelectElement).value as Locale
  emit('langChange', v)
}

function emitSerialRole(role: SerialRoleKey, e: Event) {
  emit('updateSerialRole', role, (e.target as HTMLSelectElement).value)
}

function emitCameraDraft(e: Event) {
  emit('update:cameraDraft', (e.target as HTMLTextAreaElement).value)
}

function emitKeyboardEnabled(e: Event) {
  emit('update:keyboardEnabled', (e.target as HTMLInputElement).checked)
}
</script>

<template>
  <section class="min-h-0 flex-1 overflow-y-auto bg-pve-panel p-4 text-sm">
    <div class="mx-auto max-w-3xl">
      <div class="mb-4 border-b border-pve-border pb-3">
        <h2 class="text-sm font-semibold uppercase tracking-wide text-pve-text">{{ t('settings.title') }}</h2>
      </div>

      <!-- -------- 模块：语言设置 -------- -->
      <section class="mb-6">
        <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">
          {{ t('settings.langSection') }}
        </h3>
        <label class="mb-1 block text-xs text-pve-muted">{{ t('settings.langLabel') }}</label>
        <select
          class="w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-xs text-pve-text focus:border-pve-accent focus:outline-none"
          :value="locale"
          @change="emitLangChange"
        >
          <option value="en">{{ t('settings.lang.en') }}</option>
          <option value="zh">{{ t('settings.lang.zh') }}</option>
        </select>
      </section>

      <!-- -------- 模块：摄像头设置 -------- -->
      <section class="mb-6">
        <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('video.section') }}</h3>
        <label class="mb-1 block text-xs text-pve-muted">{{ t('video.label') }}</label>
        <textarea
          :value="cameraDraft"
          rows="3"
          class="mb-2 w-full resize-y rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-xs text-pve-text placeholder:text-pve-muted focus:border-pve-accent focus:outline-none"
          :placeholder="t('video.placeholder')"
          @input="emitCameraDraft"
        />
        <p class="mb-3 text-xs leading-relaxed text-pve-muted">{{ t('video.hint') }}</p>
        <div class="flex flex-wrap gap-2">
          <button type="button" class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent" @click="emit('saveSettings')">
            {{ t('video.saveApply') }}
          </button>
          <button type="button" class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-muted hover:text-pve-warn" @click="emit('clearStoredCamera')">
            {{ t('video.clearUrl') }}
          </button>
        </div>
      </section>

      <!-- -------- 模块：串口设置 -------- -->
      <section class="mb-6">
        <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('serial.section') }}</h3>
        <div class="mb-2 flex items-center gap-2">
          <button
            type="button"
            class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs font-semibold text-pve-text hover:bg-pve-header"
            :disabled="serialListLoading"
            @click="emit('refreshSerialDevices')"
          >
            {{ serialListLoading ? t('serial.scanning') : t('serial.refresh') }}
          </button>
          <span class="font-mono text-[10px] text-pve-muted">OS: {{ serialHostOS || '—' }}</span>
        </div>
        <p v-if="serialHostOS && serialHostOS !== 'linux'" class="mb-3 text-xs text-pve-warn">
          {{ t('serial.nonlinux') }}
        </p>
        <p class="mb-3 text-xs leading-relaxed text-pve-muted">{{ t('serial.hint') }}</p>

        <div v-for="role in serialRoleKeys" :key="role" class="mb-3">
          <label class="mb-1 block text-xs text-pve-muted">{{ serialRoleTitle(role) }}</label>
          <select
            :value="serialRolesDraft[role]"
            class="w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-[11px] text-pve-text focus:border-pve-accent focus:outline-none"
            @change="emitSerialRole(role, $event)"
          >
            <option value="">{{ t('serial.unassigned') }}</option>
            <option v-for="d in serialDevices" :key="role + d.path" :value="d.path">
              {{ deviceLabel(d) }}
            </option>
          </select>
        </div>
        <p v-if="serialHostOS === 'linux' && !serialListLoading && serialDevices.length === 0" class="text-xs text-pve-warn">
          {{ t('serial.emptyList') }}
        </p>
      </section>

      <!-- -------- 模块：连接与更新 -------- -->
      <section class="mb-6">
        <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('conn.section') }}</h3>
        <button type="button" class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs font-semibold text-pve-text hover:bg-pve-header" @click="emit('reconnectWebSocket')">
          {{ t('conn.reconnectWs') }}
        </button>
        <p class="mt-2 text-xs text-pve-muted">{{ t('conn.hint') }}</p>
      </section>

      <section class="mb-6">
        <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('update.title') }}</h3>
        <button
          type="button"
          class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs font-semibold text-pve-text hover:bg-pve-header disabled:opacity-50"
          :disabled="updateCheckBusy"
          @click="emit('checkUpdate')"
        >
          {{ updateCheckBusy ? t('update.checking') : t('update.checkNow') }}
        </button>
        <p v-if="updateStatus?.enabled" class="mt-2 font-mono text-[11px] text-pve-accent2">{{ updateShasLine }}</p>
        <p v-if="updateCheckMessage" class="mt-2 text-xs leading-relaxed text-pve-muted">{{ updateCheckMessage }}</p>
        <p v-if="updateStatus?.git_error" class="mt-2 font-mono text-[11px] text-pve-warn">{{ t('update.gitErr') }} {{ updateStatus.git_error }}</p>
      </section>

      <!-- -------- 模块：控制与 ROS 说明 -------- -->
      <section class="mb-6">
        <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('ctrl.section') }}</h3>
        <label class="flex cursor-pointer items-center gap-2 text-xs text-pve-text">
          <input
            :checked="keyboardEnabled"
            type="checkbox"
            class="accent-pve-accent"
            @change="emitKeyboardEnabled"
          />
          {{ t('ctrl.keyboard') }}
        </label>
        <p class="mt-2 text-xs text-pve-muted">{{ t('ctrl.hint') }}</p>
      </section>

      <section class="rounded border border-dashed border-pve-border bg-pve-bg/80 p-3">
        <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">{{ t('ros.section') }}</h3>
        <p class="text-xs leading-relaxed text-pve-muted">{{ t('ros.body') }}</p>
      </section>
    </div>
  </section>
</template>
