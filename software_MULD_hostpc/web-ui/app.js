// 配置
const API_BASE_URL = 'http://localhost:5000'; // 根据实际情况修改
let isPortOpen = false;
let serialSocket = null;

// 初始化
document.addEventListener('DOMContentLoaded', function() {
    // 显示API地址
    document.getElementById('api-url').textContent = API_BASE_URL;
    
    // 绑定事件
    document.getElementById('open-btn').addEventListener('click', openSerialPort);
    document.getElementById('close-btn').addEventListener('click', closeSerialPort);
    document.getElementById('send-btn').addEventListener('click', sendCommand);
    document.getElementById('clear-log').addEventListener('click', clearLog);
    document.getElementById('command-input').addEventListener('keypress', function(e) {
        if (e.key === 'Enter') sendCommand();
    });
    
    // 设置摄像头画面
    document.getElementById('camera-stream').src = `${API_BASE_URL}/camera`;
    
    // 加载串口列表
    loadSerialPorts();
    
    // 更新状态
    updateStatus('控制面板已加载');
});

// 加载可用串口列表
async function loadSerialPorts() {
    try {
        const response = await fetch(`${API_BASE_URL}/api/serial/ports`);
        if (!response.ok) throw new Error('获取串口列表失败');
        const ports = await response.json();
        
        const select = document.getElementById('port-select');
        select.innerHTML = '';
        
        if (ports.length === 0) {
            select.innerHTML = '<option value="">无可用串口</option>';
        } else {
            ports.forEach(port => {
                const option = document.createElement('option');
                option.value = port;
                option.textContent = port;
                select.appendChild(option);
            });
        }
        
        updateStatus('串口列表已加载');
    } catch (error) {
        showError('无法加载串口列表: ' + error.message);
    }
}

// 打开串口
async function openSerialPort() {
    const port = document.getElementById('port-select').value;
    const baudRate = parseInt(document.getElementById('baud-input').value) || 115200;
    
    if (!port) {
        alert('请先选择一个串口！');
        return;
    }
    
    updateStatus(`正在打开串口 ${port}...`);
    
    try {
        const response = await fetch(`${API_BASE_URL}/api/serial/open`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ portName: port, baudRate: baudRate })
        });
        
        if (response.ok) {
            isPortOpen = true;
            document.getElementById('open-btn').disabled = true;
            document.getElementById('close-btn').disabled = false;
            document.getElementById('send-btn').disabled = false;
            document.getElementById('connected-device').textContent = port;
            updateStatus(`串口 ${port} 已打开`);
            clearLog(); // 打开串口时清空日志
            startSerialWebSocket();
        } else {
            throw new Error('服务器返回错误');
        }
    } catch (error) {
        showError('打开串口失败: ' + error.message);
        updateStatus('打开串口失败');
    }
}

// 关闭串口
async function closeSerialPort() {
    updateStatus('正在关闭串口...');
    
    try {
        const response = await fetch(`${API_BASE_URL}/api/serial/close`, {
            method: 'POST'
        });
        
        if (response.ok) {
            isPortOpen = false;
            document.getElementById('open-btn').disabled = false;
            document.getElementById('close-btn').disabled = true;
            document.getElementById('send-btn').disabled = true;
            document.getElementById('connected-device').textContent = '无';
            updateStatus('串口已关闭');
            stopSerialWebSocket();
        } else {
            throw new Error('服务器返回错误');
        }
    } catch (error) {
        showError('关闭串口失败: ' + error.message);
        updateStatus('关闭串口失败');
    }
}

// 发送命令
async function sendCommand() {
    const command = document.getElementById('command-input').value.trim();
    
    if (!command) {
        alert('请输入要发送的命令！');
        return;
    }
    
    if (!isPortOpen) {
        alert('请先打开串口！');
        return;
    }
    
    try {
        const response = await fetch(`${API_BASE_URL}/api/serial/send`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ data: command })
        });
        
        if (response.ok) {
            appendToLog(`[发送] ${command}`);
            document.getElementById('command-input').value = '';
        } else {
            throw new Error('发送失败');
        }
    } catch (error) {
        showError('发送命令失败: ' + error.message);
    }
}

// 使用 WebSocket 方式接收串口数据
function startSerialWebSocket() {
    if (serialSocket) {
        serialSocket.close();
    }
    // ws 协议地址
    let wsUrl = API_BASE_URL.replace(/^http/, 'ws') + '/ws/serial';
    serialSocket = new WebSocket(wsUrl);

    serialSocket.onopen = () => {
        updateStatus('串口数据流已连接');
    };
    serialSocket.onmessage = (event) => {
        try {
            // 后端推送文本或 JSON
            let msg = event.data;
            // 兼容后端未来可能推送 JSON 状态
            try {
                let obj = JSON.parse(msg);
                if (obj.type === 'status') {
                    updateStatus(obj.status);
                    return;
                }
                msg = obj.data || msg;
            } catch { /* 非 JSON 忽略 */ }
            appendToLog(`[接收] ${msg}`);
        } catch (e) {
            console.error('解析数据失败:', e);
        }
    };
    serialSocket.onerror = (err) => {
        console.error('WebSocket 连接错误:', err);
        if (isPortOpen) {
            updateStatus('串口数据流中断，尝试重连...');
            setTimeout(startSerialWebSocket, 3000);
        } else {
            updateStatus('串口数据流连接失败');
        }
    };
    serialSocket.onclose = (e) => {
        if (isPortOpen) {
            updateStatus('串口数据流已断开，尝试重连...');
            setTimeout(startSerialWebSocket, 3000);
        } else {
            updateStatus('串口数据流已断开');
        }
    };
}

function stopSerialWebSocket() {
    if (serialSocket) {
        serialSocket.close();
        serialSocket = null;
    }
}

// 辅助函数
function appendToLog(text) {
    const logElement = document.getElementById('serial-log');
    const timestamp = new Date().toLocaleTimeString();
    logElement.textContent += `[${timestamp}] ${text}\n`;
    logElement.scrollTop = logElement.scrollHeight;
}

function clearLog() {
    document.getElementById('serial-log').textContent = '';
    appendToLog('日志已清空');
}

function updateStatus(text) {
    document.getElementById('status-text').textContent = text;
    console.log(`状态: ${text}`);
}

function showError(message) {
    alert(`错误: ${message}`);
    console.error(message);
}