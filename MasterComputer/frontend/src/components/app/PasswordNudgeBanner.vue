<script setup lang="ts">
/*
|--------------------------------------------------------------------------
| 改密提醒条组件
|--------------------------------------------------------------------------
| 当默认密码提醒被临时关闭后，在主界面顶部持续提供改密入口。
|--------------------------------------------------------------------------
*/
import { t } from '../../i18n'

const emit = defineEmits<{
  open: []
}>()
</script>

<template>
  <div class="flex shrink-0 items-center justify-between gap-2 border-b border-amber-600/40 bg-amber-900/25 px-3 py-1.5 text-xs text-amber-200">
    <span>{{ t('auth.bannerNudge') }}</span>
    <button
      type="button"
      class="rounded border border-amber-500/50 px-2 py-0.5 font-semibold text-amber-100 hover:bg-amber-800/40"
      @click="emit('open')"
    >
      {{ t('auth.pwdNudgeChange') }}
    </button>
  </div>
</template>
