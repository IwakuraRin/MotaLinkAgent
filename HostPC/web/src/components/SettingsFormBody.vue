<script setup lang="ts">
import { t, setLocale, type Locale } from '../i18n'

const SERIAL_ROLE_KEYS = ['esp32_uart', 'aux_serial'] as const
type SerialRoleKey = (typeof SERIAL_ROLE_KEYS)[number]
type SerialDev = { path: string; target: string; kind: string }

defineProps<{
  localeVal: Locale
  serialDevices: SerialDev[]
  serialListLoading: boolean
  hostOs: string
}>()

const cameraUrl = defineModel<string>('cameraUrl', { required: true })
const serialRolesDraft = defineModel<Record<SerialRoleKey, string>>('serialRolesDraft', { required: true })
const maxLogLines = defineModel<number>('maxLogLines', { required: true })
const keyboardEnabled = defineModel<boolean>('keyboardEnabled', { required: true })

const emit = defineEmits<{
  save: []
  clearCamera: []
  refreshSerial: []
  reconnectWs: []
}>()

function onLangChange(e: Event) {
  const v = (e.target as HTMLSelectElement).value as Locale
  if (v === 'en' || v === 'zh' || v === 'ko') setLocale(v)
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
</script>

<template>
  <div class="min-h-0 flex-1 overflow-y-auto p-4 text-sm">
    <section class="mb-6">
      <h3 class="mb-2 text-xs font-semibold uppercase tracking-wide text-pve-muted">
        {{ t('settings.langSection') }}
      </h3>
      <label class="mb-1 block text-xs text-pve-muted">{{ t('settings.langLabel') }}</label>
      <select
        class="w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-xs text-pve-text focus:border-pve-accent focus:outline-none"
        :value="localeVal"
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
        v-model="cameraUrl"
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
          @click="emit('save')"
        >
          {{ t('video.saveApply') }}
        </button>
        <button
          type="button"
          class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-muted hover:text-pve-warn"
          @click="emit('clearCamera')"
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
          @click="emit('refreshSerial')"
        >
          {{ serialListLoading ? t('serial.scanning') : t('serial.refresh') }}
        </button>
        <span class="font-mono text-[10px] text-pve-muted">OS: {{ hostOs || '—' }}</span>
      </div>
      <p v-if="hostOs && hostOs !== 'linux'" class="mb-3 text-xs text-pve-warn">
        {{ t('serial.nonlinux') }}
      </p>
      <p class="mb-3 text-xs leading-relaxed text-pve-muted">{{ t('serial.hint') }}</p>

      <div v-for="role in SERIAL_ROLE_KEYS" :key="role" class="mb-3">
        <label class="mb-1 block text-xs text-pve-muted">{{ serialRoleTitle(role) }}</label>
        <select
          v-model="serialRolesDraft[role]"
          class="w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-[11px] text-pve-text focus:border-pve-accent focus:outline-none"
        >
          <option value="">{{ t('serial.unassigned') }}</option>
          <option v-for="d in serialDevices" :key="role + d.path" :value="d.path">
            {{ deviceLabel(d) }}
          </option>
        </select>
      </div>
      <p
        v-if="hostOs === 'linux' && !serialListLoading && serialDevices.length === 0"
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
        @click="emit('reconnectWs')"
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
</template>
