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
        :status="status"
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

<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue';
import CameraView from './components/CameraView.vue';
import SerialControl from './components/SerialControl.vue';
import SerialLog from './components/SerialLog.vue';

const envApiBase = (import.meta.env.VITE_API_BASE_URL || '').replace(/\/$/, '');
const apiBaseUrlDisplay = envApiBase || '当前域名(通过 Vite 代理)';
const today = new Date().toLocaleDateString();

const ports = ref([]);
const selectedPort = ref('');
const baudRate = ref(115200);
const isPortOpen = ref(false);
const command = ref('');
const connectedDevice = ref('');
const status = ref('准备就绪');
const serialLog = ref('');
const eventSource = ref(null);
const reconnectTimer = ref(null);

const cameraUrl = ref(`${envApiBase}/camera` || '/camera');

function apiUrl(path) {
  return `${envApiBase}${path}`;
}

function log(message) {
  const timestamp = new Date().toLocaleTimeString();
  serialLog.value += `[${timestamp}] ${message}\n`;
}

async function loadSerialPorts() {
  try {
    status.value = '正在加载串口列表...';
    const response = await fetch(apiUrl('/api/serial/ports'));
    if (!response.ok) throw new Error('获取串口列表失败');

    const list = await response.json();
    ports.value = Array.isArray(list) ? list : [];
    if (!selectedPort.value && ports.value.length > 0) {
      selectedPort.value = ports.value[0];
    }
    status.value = ports.value.length > 0 ? '串口列表已加载' : '无可用串口';
  } catch (error) {
    status.value = `加载串口失败: ${error.message}`;
    log(`错误: ${error.message}`);
  }
}

async function openSerialPort() {
  if (!selectedPort.value) {
    log('请先选择一个串口');
    return;
  }

  status.value = `正在打开串口 ${selectedPort.value}...`;

  try {
    const response = await fetch(apiUrl('/api/serial/open'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        portName: selectedPort.value,
        baudRate: Number(baudRate.value) || 115200,
      }),
    });

    if (!response.ok) throw new Error('服务器返回错误');

    isPortOpen.value = true;
    connectedDevice.value = selectedPort.value;
    status.value = `串口 ${selectedPort.value} 已打开`;
    log(`串口已打开: ${selectedPort.value} @ ${baudRate.value}bps`);
    startEventSource();
  } catch (error) {
    status.value = '打开串口失败';
    log(`打开串口失败: ${error.message}`);
  }
}

async function closeSerialPort() {
  status.value = '正在关闭串口...';

  try {
    const response = await fetch(apiUrl('/api/serial/close'), { method: 'POST' });
    if (!response.ok) throw new Error('服务器返回错误');

    isPortOpen.value = false;
    connectedDevice.value = '';
    status.value = '串口已关闭';
    log('串口已关闭');
    stopEventSource();
  } catch (error) {
    status.value = '关闭串口失败';
    log(`关闭串口失败: ${error.message}`);
  }
}

async function sendCommand() {
  const trimmed = command.value.trim();
  if (!trimmed) {
    log('请输入要发送的命令');
    return;
  }
  if (!isPortOpen.value) {
    log('请先打开串口');
    return;
  }

  try {
    const response = await fetch(apiUrl('/api/serial/send'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ data: trimmed }),
    });
    if (!response.ok) throw new Error('发送失败');

    log(`发送: ${trimmed}`);
    command.value = '';
  } catch (error) {
    log(`发送失败: ${error.message}`);
  }
}

function startEventSource() {
  stopEventSource();
  eventSource.value = new EventSource(apiUrl('/api/serial/stream'));

  eventSource.value.onmessage = (event) => {
    let dataText = event.data;
    try {
      const parsed = JSON.parse(event.data);
      dataText = typeof parsed === 'string' ? parsed : JSON.stringify(parsed);
    } catch {
      // plain text payload
    }
    log(`接收: ${dataText}`);
  };

  eventSource.value.onerror = () => {
    if (isPortOpen.value) {
      log('串口数据流中断，3 秒后重连...');
      clearTimeout(reconnectTimer.value);
      reconnectTimer.value = setTimeout(startEventSource, 3000);
    }
  };
}

function stopEventSource() {
  clearTimeout(reconnectTimer.value);
  reconnectTimer.value = null;
  if (eventSource.value) {
    eventSource.value.close();
    eventSource.value = null;
  }
}

function clearLog() {
  serialLog.value = '';
  log('日志已清空');
}

function onCameraError() {
  status.value = '摄像头加载失败';
  log('摄像头加载失败，请检查地址和后端服务');
}

onMounted(loadSerialPorts);
onBeforeUnmount(stopEventSource);
</script>