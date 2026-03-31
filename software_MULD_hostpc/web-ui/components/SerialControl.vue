<template>
  <section class="panel">
    <h2>SERIAL CONTROL</h2>

    <div class="control-group">
      <label>Port:</label>
      <select
        :value="selectedPort"
        @change="$emit('update:selectedPort', $event.target.value)"
      >
        <option v-if="ports.length === 0" value="">无可用串口</option>
        <option v-for="port in ports" :key="port" :value="port">
          {{ port }}
        </option>
      </select>
      <button class="btn-secondary refresh-btn" @click="$emit('reload-ports')">Refresh</button>
    </div>

    <div class="control-group">
      <label>Baud rate:</label>
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
        {{ isPortOpen ? 'Connected' : 'Open' }}
      </button>
      <button @click="$emit('close')" :disabled="!isPortOpen" class="btn-danger">Close</button>
    </div>

    <!-- 键盘控制开关 -->
    <div class="control-group">
      <label>Keyboard control:</label>
      <div class="button-group">
        <button @click="$emit('toggle-keyboard')" class="btn-primary">
          {{ keyboardEnabled ? 'Stop Keyboard' : 'Start Keyboard' }}
        </button>
      </div>
      <p class="keyboard-hint">
        WASD: 小车移动 |
        Q/E: 舵机1 |
        R/F: 舵机2 |
        T/G: 舵机3 |
        Y/H: 舵机4 |
        Z/X: 舵机5
      </p>
    </div>

    <div class="control-group">
      <label>Manual command:</label>
      <div class="command-row">
        <input
          type="text"
          :value="command"
          placeholder="Type serial command..."
          @input="$emit('update:command', $event.target.value)"
          @keyup.enter="$emit('send')"
        />
        <button @click="$emit('send')" :disabled="!isPortOpen" class="btn-primary">Send</button>
      </div>
    </div>

    <div class="status-box">
      <p>Status: <strong>{{ status }}</strong></p>
      <p>Device: <strong>{{ connectedDevice || 'None' }}</strong></p>
      <p>API: <strong>{{ apiBaseUrl }}</strong></p>
    </div>
  </section>
</template>

<script>
export default {
  name: 'SerialControl',
  props: {
    // 串口列表
    ports: { type: Array, required: true },
    // 当前选择的串口
    selectedPort: { type: String, default: '' },
    // 波特率
    baudRate: { type: Number, default: 115200 },
    // 串口是否已经打开
    isPortOpen: { type: Boolean, default: false },
    // 文本框中的手动命令
    command: { type: String, default: '' },
    // 当前状态文字
    status: { type: String, default: '准备就绪' },
    // 已连接的设备名称
    connectedDevice: { type: String, default: '' },
    // 显示给用户看的 API 地址
    apiBaseUrl: { type: String, default: '' },
    // 是否开启键盘控制（由父组件 App 管）
    keyboardEnabled: { type: Boolean, default: false },
  },
  emits: [
    'update:selectedPort',
    'update:baudRate',
    'update:command',
    'open',
    'close',
    'send',
    'reload-ports',
    'toggle-keyboard',
  ],
};
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

.keyboard-hint {
  margin-top: 8px;
  font-size: 14px;
  color: #555;
}
</style>
