<script setup lang="ts">
/*
|--------------------------------------------------------------------------
| 改密对话框组件
|--------------------------------------------------------------------------
| 展示默认密码提醒和改密表单，提交与状态变更全部交给父组件处理。
|--------------------------------------------------------------------------
*/
import { t } from '../../i18n'

defineProps<{
  loggedIn: boolean
  modal: 'off' | 'nudge' | 'form'
  mustChangePassword: boolean
  pwdNudgeDismissed: boolean
  pwdCurrent: string
  newPwd1: string
  newPwd2: string
  pwdFormError: string
  pwdBusy: boolean
}>()

const emit = defineEmits<{
  backdrop: []
  dismissNudge: []
  openFormFromNudge: []
  submit: []
  back: []
  'update:pwdCurrent': [value: string]
  'update:newPwd1': [value: string]
  'update:newPwd2': [value: string]
}>()

function inputValue(e: Event) {
  return (e.target as HTMLInputElement).value
}
</script>

<template>
  <Teleport to="body">
    <div
      v-if="loggedIn && modal !== 'off'"
      class="fixed inset-0 z-[200] flex items-center justify-center bg-black/60 p-4"
      role="presentation"
      @click.self="emit('backdrop')"
    >
      <div class="w-full max-w-md rounded border border-pve-border bg-pve-panel p-5 shadow-2xl" role="dialog" @click.stop>
        <template v-if="modal === 'nudge'">
          <h2 class="mb-2 text-sm font-semibold text-white">{{ t('auth.pwdNudgeTitle') }}</h2>
          <p class="mb-4 text-xs leading-relaxed text-pve-muted">{{ t('auth.pwdNudgeBody') }}</p>
          <div class="flex flex-wrap justify-end gap-2">
            <button type="button" class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header" @click="emit('dismissNudge')">
              {{ t('auth.pwdNudgeLater') }}
            </button>
            <button type="button" class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent" @click="emit('openFormFromNudge')">
              {{ t('auth.pwdNudgeChange') }}
            </button>
          </div>
        </template>

        <template v-else-if="modal === 'form'">
          <h2 class="mb-2 text-sm font-semibold text-white">{{ t('auth.pwdChangeTitle') }}</h2>
          <template v-if="!mustChangePassword">
            <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.currentPassword') }}</label>
            <input
              :value="pwdCurrent"
              type="password"
              autocomplete="current-password"
              class="mb-2 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
              @input="emit('update:pwdCurrent', inputValue($event))"
            />
          </template>
          <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.newPassword') }}</label>
          <input
            :value="newPwd1"
            type="password"
            autocomplete="new-password"
            class="mb-2 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
            @input="emit('update:newPwd1', inputValue($event))"
          />
          <label class="mb-1 block text-xs text-pve-muted">{{ t('auth.confirmPassword') }}</label>
          <input
            :value="newPwd2"
            type="password"
            autocomplete="new-password"
            class="mb-2 w-full rounded border border-pve-border bg-pve-bg px-2 py-1.5 font-mono text-sm text-pve-text focus:border-pve-accent focus:outline-none"
            @input="emit('update:newPwd2', inputValue($event))"
            @keydown.enter="emit('submit')"
          />
          <p v-if="pwdFormError" class="mb-2 font-mono text-xs text-pve-err">{{ pwdFormError }}</p>
          <div class="flex flex-wrap justify-end gap-2">
            <button v-if="mustChangePassword" type="button" class="rounded border border-pve-border bg-pve-bg px-3 py-1.5 text-xs text-pve-text hover:bg-pve-header" @click="emit('back')">
              {{ t('auth.back') }}
            </button>
            <button
              type="button"
              class="rounded border border-pve-border bg-pve-header px-3 py-1.5 text-xs font-semibold text-white hover:bg-pve-accent disabled:opacity-50"
              :disabled="pwdBusy"
              @click="emit('submit')"
            >
              {{ pwdBusy ? t('auth.busy') : t('auth.submit') }}
            </button>
          </div>
        </template>
      </div>
    </div>
  </Teleport>
</template>
