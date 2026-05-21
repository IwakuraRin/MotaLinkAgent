<script setup lang="ts">
/*
|--------------------------------------------------------------------------
| 登录页面组件
|--------------------------------------------------------------------------
| 只负责展示登录表单和向父组件提交用户名密码，不持有鉴权业务逻辑。
|--------------------------------------------------------------------------
*/
import { t } from '../../i18n'

defineProps<{
  loginUser: string
  loginPass: string
  loginError: string
  loginBusy: boolean
}>()

const emit = defineEmits<{
  'update:loginUser': [value: string]
  'update:loginPass': [value: string]
  submit: []
}>()

function setLoginUser(e: Event) {
  emit('update:loginUser', (e.target as HTMLInputElement).value)
}

function setLoginPass(e: Event) {
  emit('update:loginPass', (e.target as HTMLInputElement).value)
}
</script>

<template>
  <div class="flex flex-1 flex-col items-center justify-center gap-6 p-6">
    <div class="w-full max-w-sm rounded border border-pve-border bg-pve-panel p-6 shadow-xl">
      <h1 class="mb-1 text-center text-lg font-semibold text-white">{{ t('auth.loginTitle') }}</h1>
      <p class="mb-4 text-center text-xs leading-relaxed text-pve-muted">{{ t('auth.loginSubtitle') }}</p>
      <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.username') }}</label>
      <input
        :value="loginUser"
        type="text"
        autocomplete="username"
        class="mb-3 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
        @input="setLoginUser"
      />
      <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.password') }}</label>
      <input
        :value="loginPass"
        type="password"
        autocomplete="current-password"
        class="mb-3 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
        @input="setLoginPass"
        @keydown.enter="emit('submit')"
      />
      <p v-if="loginError" class="mb-2 font-mono text-xs text-pve-err">{{ loginError }}</p>
      <button
        type="button"
        class="w-full rounded border border-pve-border bg-pve-header py-2 text-sm font-semibold text-white hover:bg-pve-accent disabled:opacity-50"
        :disabled="loginBusy"
        @click="emit('submit')"
      >
        {{ loginBusy ? t('auth.busy') : t('auth.signIn') }}
      </button>
    </div>
  </div>
</template>
