# 📝 Engineering Postmortem: 系統規模對齊與 Kernel Deadlock 事件

## 1. 事件摘要 (Incident Summary)
在進行 L4 戰情室 (Edge Dashboard) 整合測試時，我們遭遇了兩個連續的系統級缺陷：

* **尺度不對齊 (Scale Misalignment)：** UI 顯示的距離落於 0.5cm ~ 40cm，未達工業級防護的公尺級別要求。
* **系統死鎖癱瘓 (System Freeze / D-State Deadlock)：** 調整參數後重啟系統，Node.js 陷入 `D (Uninterruptible Sleep)` 狀態，無法被 `kill -9` 終止，終端機卡死，迫使整台 Linux 機器必須 Reboot。

---

## 2. 根本原因分析 (Root Cause Analysis - RCA)

### 缺陷 A：物理尺度與架構預期不符
* **肇因：** Kernel Module (`mock_sensor.c`) 中的初始設定 (`distance_mm = 100`) 以及 Timer 迴圈的硬編碼邊界 (`5 ~ 400`)，停留於實驗室桌面級別的測試數據。
* **影響：** 導致上層 IT (Dashboard) 雖然邏輯正確，但接收到的資料無法反映真實的工業現場（如 AGV 或機械手臂的安全半徑通常為 1~3 公尺）。

### 缺陷 B：核心日誌風暴與 Mutex 鎖死 (The Deadlock)
* **肇因 1 (Log Storm)：** 原本的急停邏輯沒有實作「狀態機 (State Machine)」。當機台進入 `< 1000mm` 的紅線區時，Timer 以每秒 10 次的頻率觸發 `printk(KERN_EMERG)`，瞬間塞爆 TTY 終端機緩衝區。
* **肇因 2 (Mutex Deadlock)：** Node.js 的 `console.log` 被 TTY 阻塞，導致 Event Loop 停擺。更致命的是，在頻繁的 I/O 擁塞中，若 Kernel Timer 拿了 `mutex_lock` 卻因異常沒有執行到 `mutex_unlock`，下一次 Node.js 呼叫 `ioctl` 時就會永遠卡在等待鎖的狀態，形成殺不死的 `D` 狀態殭屍行程。

---

## 3. 解決方案與行動項目 (Resolution & Action Items)

### ✔️ Action 1: 物理參數擴增 (Parameter Alignment)
* 將 Kernel 的活動範圍擴充為 `500mm ~ 3500mm` (0.5m ~ 3.5m)。
* 將急停防線設定為 `1000mm` (1 公尺)。
* 同步修改 `init` 的初始狀態為 `1000`，確保系統啟動瞬間不會引發誤報。

### ✔️ Action 2: 實作狀態機降噪 (State Machine for Telemetry)
* 在 `mock_sensor_dev` 結構中加入 `last_status`。
* 只有在「狀態發生變化 (Edge-triggered)」的瞬間，才發送 `printk` 系統日誌，徹底根除 Log 風暴。

### ✔️ Action 3: 確保解鎖與優雅退出 (Defensive Engineering)
* 嚴格規範 `mock_sensor.c` 中的 Mutex 邊界，確保在任何條件分支下，`mutex_unlock` 都必定會被執行。
* 在 Node.js (`adapter.js`) 實作完整的 `SIGINT` (Ctrl+C) 捕捉機制。當系統收到關閉訊號時，依序安全釋放 WebSocket、HTTP Port 3000，以及最關鍵的 `/dev/mock_sensor` file descriptor，避免殘留孤兒資源。
