<script setup lang="ts">
import { ref, watch, nextTick } from 'vue'

const props = defineProps<{
  lines: string[]
}>()

const preRef = ref<HTMLElement | null>(null)

watch(
  () => props.lines.length,
  async () => {
    await nextTick()
    const el = preRef.value
    if (el) el.scrollTop = el.scrollHeight
  },
)
</script>

<template>
  <pre
    ref="preRef"
    class="m-0 h-full overflow-auto p-2 font-mono text-[10px] leading-snug text-[#9bdc9b]"
    >{{ lines.join('\n') }}</pre
  >
</template>
