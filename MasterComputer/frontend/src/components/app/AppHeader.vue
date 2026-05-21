<script setup lang="ts">
/*
|--------------------------------------------------------------------------
| 顶栏组件
|--------------------------------------------------------------------------
| 展示系统标题、当前主机、登录用户、WebSocket 状态和账户操作按钮。
|--------------------------------------------------------------------------
*/
import { t } from '../../i18n'

defineProps<{
  hostDisplay: string
  authUsername: string
  statusColor: string
  wsStateLabel: string
  mustChangePassword: boolean
}>()

const emit = defineEmits<{
  changePassword: []
  logout: []
}>()
</script>

<template>
  <header class="flex h-9 shrink-0 items-center border-b border-pve-border bg-gradient-to-b from-[#454545] to-[#3a3a3a] px-3 text-sm shadow">
    <span class="font-semibold tracking-tight text-white">OmniRoam</span>
    <span class="mx-2 text-pve-muted">|</span>
    <span class="text-pve-muted">{{ t('app.subtitle') }}</span>
    <span class="ml-6 font-mono text-xs text-pve-accent2">{{ hostDisplay }}</span>
    <div class="ml-auto flex items-center gap-3 font-mono text-xs">
      <span class="text-pve-muted">{{ authUsername }}</span>
      <span :class="statusColor">● {{ wsStateLabel }}</span>
      <button
        v-if="!mustChangePassword"
        type="button"
        class="rounded border border-pve-border bg-pve-panel px-2 py-0.5 text-[11px] font-semibold uppercase tracking-wide text-pve-text shadow hover:bg-pve-header"
        @click="emit('changePassword')"
      >
        {{ t('auth.changePasswordBtn') }}
      </button>
      <button
        type="button"
        class="rounded border border-pve-border bg-pve-panel px-2 py-0.5 text-[11px] font-semibold uppercase tracking-wide text-pve-warn shadow hover:bg-pve-header"
        @click="emit('logout')"
      >
        {{ t('auth.signOut') }}
      </button>
    </div>
  </header>
</template>
