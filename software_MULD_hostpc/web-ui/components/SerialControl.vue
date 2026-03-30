<template>
  <section class="panel">
    <h2>🔌 串口控制</h2>

    <div class="control-group">
      <label>选择串口:</label>
      <select
        :value="selectedPort"
        @change="$emit('update:selectedPort', $event.target.value)"
      >
        <option v-if="ports.length === 0" value="">无可用串口</option>
        <option v-for="port in ports" :key="port" :value="port">
          {{ port }}
        </option>
      </select>
      <button class="btn-secondary refresh-btn" @click="$emit('reload-ports')">刷新串口</button>
    </div>

    <div class="control-group">
      <label>波特率:</label>
      <input
        type="number"
        min="9600"
        max="1152000"
        :value="baudRate"
        @input="$emit('update:baudRate', Number($event.target.value))"
      />
    </div>

    <div class="button-group">
      <button @click="$emit('open')" :disabled="isPortOpen" class="btn-success">
        {{ isPortOpen ? '已连接' : '打开串口' }}
      </button>
      <button @click="$emit('close')" :disabled="!isPortOpen" class="btn-danger">关闭串口</button>
    </div>

    <div class="control-group">
      <label>发送命令:</label>
      <div class="command-row">
        <input
          type="text"
          :value="command"
          placeholder="输入串口指令..."
          @input="$emit('update:command', $event.target.value)"
          @keyup.enter="$emit('send')"
        />
        <button @click="$emit('send')" :disabled="!isPortOpen" class="btn-primary">发送</button>
      </div>
    </div>

    <div class="status-box">
      <p>状态: <strong>{{ status }}</strong></p>
      <p>已连接设备: <strong>{{ connectedDevice || '无' }}</strong></p>
      <p>API地址: <strong>{{ apiBaseUrl }}</strong></p>
    </div>
  </section>
</template>

<script setup>
defineProps({
  ports: { type: Array, required: true },
  selectedPort: { type: String, default: '' },
  baudRate: { type: Number, default: 115200 },
  isPortOpen: { type: Boolean, default: false },
  command: { type: String, default: '' },
  status: { type: String, default: '准备就绪' },
  connectedDevice: { type: String, default: '' },
  apiBaseUrl: { type: String, default: '' },
});

defineEmits([
  'update:selectedPort',
  'update:baudRate',
  'update:command',
  'open',
  'close',
  'send',
  'reload-ports',
]);
</script>

<style scoped>
.command-row {
  display: flex;
  gap: 10px;
}

.command-row input {
  flex: 1;
}

.refresh-btn {
  margin-top: 8px;
}
</style>
