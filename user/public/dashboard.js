// 連線到與網頁同一個 Host 的 WebSocket 伺服器
const ws = new WebSocket(`ws://${window.location.host}`);

// --- DOM 元素綁定 ---
const els = {
    distance: document.getElementById('distance-val'),
    status: document.getElementById('system-status'),
    pm25: document.getElementById('pm25-val'),
    noise: document.getElementById('noise-val'),
    rfid: document.getElementById('rfid-val'),
    timestamp: document.getElementById('timestamp-val'),
    banner: document.getElementById('connection-banner'),
    connStatusText: document.getElementById('conn-status-text'),
    sopInstruction: document.getElementById('sop-instruction'),
    safetyPanel: document.getElementById('safety-panel')
};

// --- Watchdog 看門狗計時器 ---
let watchdogTimer = null;
const WATCHDOG_TIMEOUT_MS = 3000; // 3秒沒收到資料就視為斷線

function resetWatchdog() {
    clearTimeout(watchdogTimer);
    
    // 恢復正常連線 UI 狀態
    els.banner.className = 'banner connected';
    els.connStatusText.innerText = '✅ 邊緣閘道器連線正常 (Edge Link Active)';
    els.sopInstruction.classList.add('hidden');
    document.body.classList.remove('disconnected');

    // 設定新的計時器
    watchdogTimer = setTimeout(triggerGracefulDegradation, WATCHDOG_TIMEOUT_MS);
}

// 斷線時的優雅降級處理 (SOP 牧羊犬機制)
function triggerGracefulDegradation() {
    console.warn("Watchdog timeout! UI is gracefully degrading.");
    els.banner.className = 'banner disconnected';
    els.connStatusText.innerText = '❌ IT 連線中斷 (Connection Lost)';
    els.sopInstruction.classList.remove('hidden'); // 顯示防呆 SOP
    
    // 將畫面變灰，鎖定在「最後已知狀態」
    document.body.classList.add('disconnected');
    els.status.innerText = "LAST KNOWN STATE";
    els.status.className = "status-badge normal";
}

// --- WebSocket 事件處理 ---
ws.onopen = () => {
    console.log("WebSocket connection established.");
    resetWatchdog();
};

ws.onmessage = (event) => {
    // 1. 只要收到資料，立刻餵食看門狗 (重置計時器)
    resetWatchdog();

    // 2. 解析 Gateway 傳來的 JSON
    const payload = JSON.parse(event.data);
    
    // 3. 更新次要環境資訊 (IT Layer)
    els.timestamp.innerText = new Date(payload.timestamp).toLocaleTimeString();
    els.pm25.innerText = payload.environment_subsystem.pm25;
    els.noise.innerText = payload.environment_subsystem.noise_db;
    els.rfid.innerText = payload.access_subsystem.last_scan;

    // 4. 更新核心工安資訊 (OT Layer)
    if (payload.safety_subsystem) {
        const distance = payload.safety_subsystem.distance_mm / 10; // mm 轉 cm
        const status = payload.safety_subsystem.status;

        els.distance.innerText = distance.toFixed(1);

        // --- 升級的防漏網邏輯與光暈同步 ---
        if (status === "EMERGENCY_STOP" || distance <= 100) {
            // 1. 紅區：底層 OT 斷電，或者 IT 判定進入危險絕對領域
            els.distance.style.color = "#ff1744"; // 更亮的螢光紅
            els.distance.style.textShadow = "0 0 25px rgba(255, 23, 68, 0.6)"; // 同步改為紅光
            
            els.status.innerText = status === "EMERGENCY_STOP" ? "KILLED BY FAIL-SAFE" : "CRITICAL ZONE";
            els.status.className = "status-badge danger";
        } else if (distance <= 200) {
            // 2. 黃區：100 < distance <= 200
            els.distance.style.color = "#ffea00"; 
            els.distance.style.textShadow = "0 0 25px rgba(255, 234, 0, 0.6)"; // 黃光
            
            els.status.innerText = "WARNING: APPROACHING";
            els.status.className = "status-badge warning";
        } else {
            // 3. 綠區：大於 200cm
            els.distance.style.color = "#00e676"; 
            els.distance.style.textShadow = "0 0 25px rgba(0, 230, 118, 0.6)"; // 綠光
            
            els.status.innerText = "RUNNING";
            els.status.className = "status-badge normal";
        }
    }
};

ws.onclose = () => {
    triggerGracefulDegradation(); // 伺服器關閉時立刻降級
};