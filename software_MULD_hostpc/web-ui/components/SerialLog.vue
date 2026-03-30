<template>
  <section class="panel log-panel">
    <h2>📋 串口日志</h2>
    <div class="log-box" ref="logBox">
      <pre>{{ serialLog || '等待串口数据...' }}</pre>
    </div>
    <button @click="$emit('clear')" class="btn-secondary">清空日志</button>
  </section>
</template>

<script setup>
import { nextTick, ref, watch } from 'vue';

const props = defineProps({
  serialLog: {
    type: String,
    default: '',
  },
});

defineEmits(['clear']);

const logBox = ref(null);

watch(
  () => props.serialLog,
  async () => {
    await nextTick();
    if (logBox.value) {
      logBox.value.scrollTop = logBox.value.scrollHeight;
    }
  }
);
</script>

<style scoped>
.log-panel {
  grid-column: 1 / -1;
}
</style>
