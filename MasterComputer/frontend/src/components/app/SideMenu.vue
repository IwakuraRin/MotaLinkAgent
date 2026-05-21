<script setup lang="ts">
/*
|--------------------------------------------------------------------------
| 左侧菜单组件
|--------------------------------------------------------------------------
| 控制功能页和设置页切换，页面状态由父组件持有。
|--------------------------------------------------------------------------
*/
import { t } from '../../i18n'
import type { MenuKey } from '../../appTypes'

defineProps<{
  activeMenu: MenuKey
}>()

const emit = defineEmits<{
  'update:activeMenu': [value: MenuKey]
}>()

function itemClass(active: boolean) {
  return active
    ? 'border-pve-accent bg-pve-header text-white'
    : 'border-pve-border bg-pve-panel text-pve-muted hover:text-pve-text'
}
</script>

<template>
  <aside class="flex w-14 shrink-0 flex-col items-stretch gap-2 border-r border-pve-border bg-[#242424] px-1.5 py-2">
    <button
      type="button"
      class="min-h-14 rounded border px-1 text-xs font-semibold leading-tight transition-colors"
      :class="itemClass(activeMenu === 'function')"
      @click="emit('update:activeMenu', 'function')"
    >
      {{ t('menu.function') }}
    </button>
    <button
      type="button"
      class="min-h-14 rounded border px-1 text-xs font-semibold leading-tight transition-colors"
      :class="itemClass(activeMenu === 'settings')"
      @click="emit('update:activeMenu', 'settings')"
    >
      {{ t('menu.settings') }}
    </button>
  </aside>
</template>
