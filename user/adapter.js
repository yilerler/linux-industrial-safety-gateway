const fs = require('fs');
const path = require('path');
const ioctl = require('ioctl-napi');
const express = require('express');
const { WebSocketServer } = require('ws');
const http = require('http');

// --- 1. 建立 Web 與 WebSocket 伺服器 (IT Layer) ---
const app = express();
const server = http.createServer(app);
app.use(express.static(path.join(__dirname, 'public')));
const wss = new WebSocketServer({ server });

wss.on('connection', function connection(ws) {
    console.log('🌐 [WebUI] A new dashboard client connected!');
    ws.on('close', () => console.log('🌐 [WebUI] Dashboard client disconnected.'));
});

const PORT = 3000;
server.listen(PORT, () => {
    console.log(`🚀 [System] Edge Dashboard Server is running on http://localhost:${PORT}`);
});

// --- 2. 定義跨層合約 (Contract Definition) ---
const SENSOR_MAGIC = 'S'.charCodeAt(0);
const COMMAND_NR = 1;

// ⚠️ 核心升級：精準對齊 Kernel 的 24 Bytes (packed) 暫存器地圖
const DATA_SIZE = 24; 

// --- IOCTL 號碼計算機 ---
const _IOC_NRBITS = 8, _IOC_TYPEBITS = 8, _IOC_SIZEBITS = 14, _IOC_DIRBITS = 2;
const _IOC_NRSHIFT = 0;
const _IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS;
const _IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS;
const _IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS;
const _IOC_READ = 2;

function _IOR(type, nr, size) {
    return (_IOC_READ << _IOC_DIRSHIFT) | (size << _IOC_SIZESHIFT) | (type << _IOC_TYPESHIFT) | (nr << _IOC_NRSHIFT);
}

const IOCTL_GET_DATA = _IOR(SENSOR_MAGIC, COMMAND_NR, DATA_SIZE);
console.log(`[System] IOCTL Command Code Calculated: 0x${IOCTL_GET_DATA.toString(16)}`);

// --- 3. 連接底層驅動 (Driver Interface) ---
const DEVICE_PATH = '/dev/mock_elc'; // ⚠️ 注意：對應新的設備名稱
let fd = null;

try {
    fd = fs.openSync(DEVICE_PATH, 'r+');
    console.log(`[System] Device ${DEVICE_PATH} opened successfully (fd=${fd})`);
} catch (err) {
    console.error(`[Error] Failed to open ${DEVICE_PATH}. Did you load the mock_elc_core module?`);
    process.exit(1);
}

// 準備一個 24 bytes 的空箱子，用來接 Kernel 丟上來的記憶體區塊
const buffer = Buffer.alloc(DATA_SIZE);

// --- 4. 實作 OT 層工業通訊協定 (Hex Protocol Serialization) ---
function encodeHexProtocol(distance_mm, status_code) {
    const txBuffer = Buffer.alloc(6);
    txBuffer.writeUInt8(0xAA, 0); // Header
    txBuffer.writeUInt8(0x01, 1); // Command ID
    const safeDistance = Math.min(Math.max(0, distance_mm), 65535);
    txBuffer.writeUInt16BE(safeDistance, 2); 
    txBuffer.writeUInt8(status_code, 4); // Status: 0=NORMAL, 1=EMERGENCY
    
    // Checksum (前 5 bytes 加總取 8-bit)
    let checksum = 0;
    for (let i = 0; i < 5; i++) checksum += txBuffer.readUInt8(i);
    txBuffer.writeUInt8(checksum & 0xFF, 5);
    return txBuffer;
}

function formatHexString(buffer) {
    return [...buffer].map(b => '0x' + b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
}

// --- 5. 業務迴圈 (Pure Middleware Translator) ---
const aggregationTimer = setInterval(() => {
    try {
        // 1. 向 Kernel (Modbus Engine) 發起輪詢
        const ret = ioctl(fd, IOCTL_GET_DATA, buffer);
        
        if (ret === 0) {
            // 2. 🗄️ 絕對精準的記憶體映射解碼 (Deserialization)
            // 完全依賴 C struct 的 byte offset
            const distance = buffer.readInt32LE(4);
            const status_code = buffer.readInt32LE(8);
            const pm25_val = buffer.readInt32LE(12);
            const noise_val = buffer.readInt32LE(16);
            const rfid_raw = buffer.readInt32LE(20);

            const status_str = status_code === 1 ? "EMERGENCY_STOP" : "NORMAL";
            const rfid_str = rfid_raw > 0 ? `CARD_${rfid_raw.toString().padStart(4, '0')}` : "NO_CARD";

            // 3. 轉化為標準 JSON Payload (IT 軌道)
            const systemPayload = {
                timestamp: new Date().toISOString(),
                safety_subsystem: {
                    distance_mm: distance,
                    status: status_str
                },
                environment_subsystem: {
                    pm25: pm25_val,
                    noise_db: noise_val
                },
                access_subsystem: {
                    last_scan: rfid_str
                }
            };

            // 4. IT 軌道推播：推給所有連接的 WebSocket (戰情室)
            const jsonString = JSON.stringify(systemPayload);
            wss.clients.forEach(client => {
                if (client.readyState === 1) client.send(jsonString);
            });

            // 5. OT 軌道推播：翻譯成 Hex 封包並印出 (模擬向下派發)
            const hexBuffer = encodeHexProtocol(distance, status_code);
            const hexString = formatHexString(hexBuffer);
            
            // 終端機業務邏輯展示 (取代原本雜亂的印出)
            if (status_str === "EMERGENCY_STOP") {
                console.error(`🚨 [ALARM] MOTOR OFFLINE! OT-UART TX -> [ ${hexString} ]`);
            } else if (rfid_raw > 0) {
                console.log(`🔑 [ACCESS] Processing login for ${rfid_str}...`);
            }
        }
    } catch (e) {
        console.error(`[Error] Aggregation failed:`, e.message);
    }
}, 200); // 每秒 5 次向 Kernel 輪詢

// --- 優雅退出 (Graceful Shutdown) ---
process.on('SIGINT', () => {
    console.log('\n[System] SIGINT (Ctrl+C) received. Initiating graceful shutdown...');
    clearInterval(aggregationTimer);
    console.log('[System] Aggregation loop stopped.');
    
    wss.close(() => console.log('[System] WebSocket server closed.'));
    server.close(() => console.log('[System] HTTP server closed.'));

    if (fd !== null) {
        try {
            fs.closeSync(fd);
            console.log(`[System] Device ${DEVICE_PATH} closed safely.`);
        } catch (e) {
            console.error(`[Error] Failed to close device: ${e.message}`);
        }
    }
    setTimeout(() => process.exit(0), 500); 
});