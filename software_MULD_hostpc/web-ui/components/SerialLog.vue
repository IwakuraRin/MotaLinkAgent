<template>
  <section class="panel log-panel">
    <h2>SYSTEM LOG</h2>
    <div class="log-box" ref="logBox">
      <pre>{{ serialLog || 'Waiting for serial data...' }}</pre>
    </div>
    <button @click="$emit('clear')" class="btn-secondary">Clear Log</button>
  </section>
</template>

<script>
import { nextTick } from 'vue';

export default {
  name: 'SerialLog',
  props: {
    // 串口日志字符串，由父组件传入
    serialLog: {
      type: String,
      default: '',
    },
  },
  emits: ['clear'],
  data() {
    return {
      // 日志容器 DOM 引用，用来自动滚动到底部
      logBox: null,
    };
  },
  watch: {
    // 监听日志内容变化，变化后把滚动条滑到最底
    async serialLog() {
      await nextTick();
      if (this.logBox) {
        this.logBox.scrollTop = this.logBox.scrollHeight;
      }
    },
  },
  mounted() {
    // 在挂载时获取真实 DOM 引用
    this.logBox = this.$refs.logBox;
  },
};
</script>

<style scoped>
.log-panel {
  grid-column: 1 / -1;
}
</style>
