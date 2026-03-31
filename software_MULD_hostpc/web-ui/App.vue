<template>
  <div class="app-container">
    <header class="app-header">
      <h1>🚀 MULD-Vehicle 控制中心</h1>
    </header>

    <main class="app-main">
      <CameraView v-model:camera-url="cameraUrl" @camera-error="onCameraError" />

      <SerialControl
        :ports="ports"
        :is-port-open="isPortOpen"
        :status="status"`
        :connected-device="connectedDevice"
        :api-base-url="apiBaseUrlDisplay"
        v-model:selected-port="selectedPort"
        v-model:baud-rate="baudRate"
        v-model:command="command"
        @open="openSerialPort"
        @close="closeSerialPort"
        @send="sendCommand"
        @reload-ports="loadSerialPorts"
      />

      <SerialLog :serial-log="serialLog" @clear="clearLog" />
    </main>

    <footer class="app-footer">
      <p>MULD-Vehicle 控制面板 v1.0 | {{ today }}</p>
    </footer>
  </div>
</template>

<script>
import CameraView from './components/CameraView.vue';
import SerialControl from './components/SerialControl.vue';
import SerialLog from './components/SerialLog.vue';

const envApiBase = (import.meta.env.VITE_API_BASE_URL || '').replace(/\/$/, '');
const today = new Date().toLocaleDateString();

export default {
  name: 'App',
  components: {
    CameraView,
    SerialControl,
    SerialLog,
  },
  data() {
    return {
      // 接口基地址（为空时走 Vite 代理）
      envApiBase,
      // 仅用于界面显示，方便确认当前后端地址
      apiBaseUrlDisplay: envApiBase || '当前域名(通过 Vite 代理)',
      // 页脚日期（页面加载时生成一次）
      today,
      // 串口相关状态
      ports: [],
      selectedPort: '',
      baudRate: 115200,
      isPortOpen: false,
      command: '',
      connectedDevice: '',
      status: '准备就绪',
      serialLog: '',
      // SSE 连接和重连定时器
      eventSource: null,
      reconnectTimer: null,
      // 摄像头地址
      cameraUrl: `${envApiBase}/camera` || '/camera',
    };
  },
  methods: {
    apiUrl(path) {
      return `${this.envApiBase}${path}`;
    },
    log(message) {
      const timestamp = new Date().toLocaleTimeString();
      this.serialLog += `[${timestamp}] ${message}\n`;
    },
    async loadSerialPorts() {
      try {
        this.status = '正在加载串口列表...';
        const response = await fetch(this.apiUrl('/api/serial/ports'));
        if (!response.ok) throw new Error('获取串口列表失败');

        const list = await response.json();
        this.ports = Array.isArray(list) ? list : [];
        if (!this.selectedPort && this.ports.length > 0) {
          this.selectedPort = this.ports[0];
        }
        this.status = this.ports.length > 0 ? '串口列表已加载' : '无可用串口';
      } catch (error) {
        this.status = `加载串口失败: ${error.message}`;
        this.log(`错误: ${error.message}`);
      }
    },
    async openSerialPort() {
      if (!this.selectedPort) {
        this.log('请先选择一个串口');
        return;
      }

      this.status = `正在打开串口 ${this.selectedPort}...`;

      try {
        const response = await fetch(this.apiUrl('/api/serial/open'), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            portName: this.selectedPort,
            baudRate: Number(this.baudRate) || 115200,
          }),
        });

        if (!response.ok) throw new Error('服务器返回错误');

        this.isPortOpen = true;
        this.connectedDevice = this.selectedPort;
        this.status = `串口 ${this.selectedPort} 已打开`;
        this.log(`串口已打开: ${this.selectedPort} @ ${this.baudRate}bps`);
        this.startEventSource();
      } catch (error) {
        this.status = '打开串口失败';
        this.log(`打开串口失败: ${error.message}`);
      }
    },
    async closeSerialPort() {
      this.status = '正在关闭串口...';

      try {
        const response = await fetch(this.apiUrl('/api/serial/close'), { method: 'POST' });
        if (!response.ok) throw new Error('服务器返回错误');

        this.isPortOpen = false;
        this.connectedDevice = '';
        this.status = '串口已关闭';
        this.log('串口已关闭');
        this.stopEventSource();
      } catch (error) {
        this.status = '关闭串口失败';
        this.log(`关闭串口失败: ${error.message}`);
      }
    },
    async sendCommand() {
      const trimmed = this.command.trim();
      if (!trimmed) {
        this.log('请输入要发送的命令');
        return;
      }
      if (!this.isPortOpen) {
        this.log('请先打开串口');
        return;
      }

      try {
        const response = await fetch(this.apiUrl('/api/serial/send'), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ data: trimmed }),
        });
        if (!response.ok) throw new Error('发送失败');

        this.log(`发送: ${trimmed}`);
        this.command = '';
      } catch (error) {
        this.log(`发送失败: ${error.message}`);
      }
    },
    startEventSource() {
      this.stopEventSource();
      this.eventSource = new EventSource(this.apiUrl('/api/serial/stream'));

      this.eventSource.onmessage = (event) => {
        let dataText = event.data;
        try {
          const parsed = JSON.parse(event.data);
          dataText = typeof parsed === 'string' ? parsed : JSON.stringify(parsed);
        } catch {
          // 后端可能直接返回纯文本，这里直接按文本记录日志
        }
        this.log(`接收: ${dataText}`);
      };

      this.eventSource.onerror = () => {
        if (this.isPortOpen) {
          this.log('串口数据流中断，3 秒后重连...');
          clearTimeout(this.reconnectTimer);
          this.reconnectTimer = setTimeout(() => this.startEventSource(), 3000);
        }
      };
    },
    stopEventSource() {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
      if (this.eventSource) {
        this.eventSource.close();
        this.eventSource = null;
      }
    },
    clearLog() {
      this.serialLog = '';
      this.log('日志已清空');
    },
    onCameraError() {
      this.status = '摄像头加载失败';
      this.log('摄像头加载失败，请检查地址和后端服务');
    },
  },
  mounted() {
    this.loadSerialPorts();
  },
  beforeUnmount() {
    this.stopEventSource();
  },
};
</script>