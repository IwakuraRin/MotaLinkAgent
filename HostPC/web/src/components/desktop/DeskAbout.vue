<script setup lang="ts">
import { t } from '../../i18n'
</script>

<template>
  <div class="h-full overflow-auto p-4 text-sm leading-relaxed text-[#b8c4d4]">
    <h2 class="mb-2 text-base font-semibold text-white">{{ t('desktop.aboutTitle') }}</h2>
    <p class="mb-3 text-xs text-[#8a9aac]">{{ t('desktop.aboutBody1') }}</p>
    <p class="mb-3 text-xs text-[#8a9aac]">{{ t('desktop.aboutBody2') }}</p>
    <ul class="list-inside list-disc space-y-1 text-xs text-[#7a8a9c]">
      <li>{{ t('desktop.aboutLi1') }}</li>
      <li>{{ t('desktop.aboutLi2') }}</li>
      <li>{{ t('desktop.aboutLi3') }}</li>
      <li>{{ t('desktop.aboutLi4') }}</li>
    </ul>
  </div>
</template>
