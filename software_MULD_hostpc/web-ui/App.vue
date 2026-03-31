<template>
  <div class="app-container">
    <header class="app-header">
      <!-- 标题靠左显示 -->
      <h1>MULD Vehicle Control Console</h1>
    </header>

    <main class="app-main">
      <!-- 左侧：两个摄像头画面（小车 + 机械臂） -->
      <section class="panel">
        <h2>VIDEO FEEDS</h2>
        <div class="camera-grid">
          <!-- 小车摄像头画面 -->
          <CameraView_CAR
            :camera-url="cameraUrlCar"
            @update:camera-url="cameraUrlCar = $event"
            @camera-error="onCameraError('小车摄像头')"
          />

          <!-- 机械臂摄像头画面 -->
          <CameraView_RoboticArm
            :camera-url="cameraUrlArm"
            @update:camera-url="cameraUrlArm = $event"
            @camera-error="onCameraError('机械臂摄像头')"
          />
        </div>
      </section>

      <SerialControl
        :ports="ports"
        :is-port-open="isPortOpen"
        :status="status"
        :connected-device="connectedDevice"
        :api-base-url="apiBaseUrlDisplay"
        v-model:selected-port="selectedPort"
        v-model:baud-rate="baudRate"
        v-model:command="command"
        :keyboard-enabled="keyboardEnabled"
        @open="openSerialPort"
        @close="closeSerialPort"
        @send="sendCommand"
        @reload-ports="loadSerialPorts"
        @toggle-keyboard="toggleKeyboardControl"
      />

      <SerialLog :serial-log="serialLog" @clear="clearLog" />
    </main>

    <footer class="app-footer">
      <p>MULD-Vehicle 控制面板 v1.0 | {{ today }}</p>
    </footer>
  </div>
</template>

<script>
import CameraView_CAR from './components/CameraView_CAR.vue';
import CameraView_RoboticArm from './components/CameraView_RoboticArm.vue';
import SerialControl from './components/SerialControl.vue';
import SerialLog from './components/SerialLog.vue';


const envApiBase = (import.meta.env.VITE_API_BASE_URL || '').replace(/\/$/, '');
const today = new Date().toLocaleDateString();

export default {
  name: 'App',
  components: {
    CameraView_CAR,
    CameraView_RoboticArm,
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
      // 左侧两个摄像头地址（默认都指向同一个接口，后面可以在界面中分别修改）
      cameraUrlCar: `${envApiBase}/camera` || '/camera',
      cameraUrlArm: `${envApiBase}/camera` || '/camera',
      // 是否开启键盘控制（WASD/QE/RF/TG/YH/ZX）
      keyboardEnabled: false,
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
    // 某个摄像头加载失败时的回调
    onCameraError(source) {
      this.status = `${source || '摄像头'}加载失败`;
      this.log(`${source || '摄像头'}加载失败，请检查地址和后端服务`);
    },
    // 开关键盘控制（绑定在右侧 Start 按钮）
    toggleKeyboardControl() {
      // 只有串口打开时才允许开启键盘控制
      if (!this.isPortOpen && !this.keyboardEnabled) {
        this.log('请先打开串口，再开启键盘控制');
        return;
      }
      this.keyboardEnabled = !this.keyboardEnabled;
      this.log(this.keyboardEnabled ? '键盘控制已开启' : '键盘控制已关闭');
    },
    // 监听全局键盘按键，把按键映射成串口指令
    handleKeydown(event) {
      if (!this.keyboardEnabled) return;
      if (!this.isPortOpen) return;

      const key = event.key.toLowerCase();

      // 键位说明：小车 + 机械臂各个舵机
      const map = {
        w: '小车前进',
        s: '小车后退',
        a: '小车左转',
        d: '小车右转',
        q: '机械臂舵机1 正向',
        e: '机械臂舵机1 反向',
        r: '机械臂舵机2 正向',
        f: '机械臂舵机2 反向',
        t: '机械臂舵机3 正向',
        g: '机械臂舵机3 反向',
        y: '机械臂舵机4 正向',
        h: '机械臂舵机4 反向',
        z: '机械臂舵机5 正向',
        x: '机械臂舵机5 反向',
      };

      if (!map[key]) return;

      // 先在日志里写明按键对应的动作
      this.log(`键盘 ${key.toUpperCase()} -> ${map[key]}`);

      // 将按键本身通过串口发下去（下位机按自己的协议解析）
      this.command = key;
      this.sendCommand();
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