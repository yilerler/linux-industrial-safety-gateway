# POSTMORTEM-002: 物理 I/O 阻塞盲點與 Kernel Thread 遷移藍圖

## 1. 現況剖析 (Observation)
在 V4.2 架構中，我們將邊緣邏輯控制器 (ELC) 與 Modbus 輪詢引擎實作於 Linux 核心的 `mod_timer` 中。透過 [ADR-005](ADR-005-smp-stress-test-and-o1-deepcopy.md) 的壓測證明，在隔離實體 I/O 的純軟體模擬下，基於 Spinlock 與 O(1) 深拷貝的架構能達成每秒 85 萬次的高頻併發，展現了極致的記憶體與併發安全。

然而，以真實工業物聯網 (IIoT) 的物理標準審視，當前架構存在一個致命的「環境不匹配 (Context Mismatch)」盲點，這將是系統對接真實 OT 設備時最大的隱患。

## 2. 核心盲點與致命隱患 (The Blind Spot & Critical Risks)

### 2.1 物理 I/O 的阻塞現實 (The Reality of Blocking I/O)
真實的工業通訊（如 RS-485 UART 讀取 Modbus RTU 暫存器）必然伴隨數毫秒 (ms) 級別的物理網路延遲。這意味著南向硬體抽象層 (HAL) 的讀取函式必須具備「等待 (Block/Sleep)」的能力。

### 2.2 原子上下文的睡眠禁忌 (Sleep-in-Atomic Panic)
目前的 `mod_timer` 運行於**軟中斷上下文 (Softirq / Atomic Context)**。Linux 核心嚴格規定，在此上下文中絕對不允許任何可能導致執行緒睡眠的行為（如 `msleep` 或阻塞型 I/O 讀取）。
若未來直接將真實 RS-485 讀取邏輯掛載於當前的 HAL 中，當 CPU 等待 UART 中斷時，將觸發 `scheduling while atomic` 致命錯誤，導致整台 Edge Gateway 瞬間 Kernel Panic 暴斃。

### 2.3 軟即時排程抖動 (Soft Real-Time Jitter)
`mod_timer` 的觸發受限於系統整體的軟即時排程，其時間精度與抖動 (Jitter) 無法提供等同於實體 PLC（可程式化邏輯控制器）的絕對決定性 (Determinism)。

## 3. V5.0 架構重構藍圖 (Migration Blueprint)

為徹底解決上述物理限制，確立系統從「軟體定義原型」邁向「量產級物理設備」的演進路線，本專案明定下一階段 (V5.0) 的重構藍圖如下：

### 3.1 遷移至內核執行緒 (Kernel Thread Migration)
將 ELC 的核心輪詢引擎從 `mod_timer` 全面遷移至 **`kthread` (內核執行緒)** 或 **`Workqueue` (工作佇列)**。
* **架構價值：** `kthread` 運行於**進程上下文 (Process Context)**，允許合法的睡眠與排程切換。這使得 HAL 層能安全地呼叫阻塞型 I/O API，完美適配 RS-485 等慢速物理設備。

### 3.2 導入等待佇列與中斷驅動 (Wait Queue & Interrupt-Driven I/O)
廢除忙等待 (Busy-waiting) 模擬，改為在 `kthread` 中結合 **`wait_queue_head_t` (等待佇列)**。當發起 Modbus 讀取時，執行緒主動讓出 CPU (Yield) 進入睡眠，直到底層 UART 硬體中斷 (Hardware Interrupt) 喚醒佇列，以最高效的方式利用 CPU 資源。

### 3.3 賦予硬即時排程優先級 (RT Scheduling Policy)
為了消除排程抖動，將負責 OT 輪詢的 `kthread` 提升至即時排程策略（如 **`SCHED_FIFO`** 或 **`SCHED_RR`**），賦予其高於一般 User Space 行程（含 Node.js 中介層）的絕對搶占權 (Preemption)。藉此確保工安急停狀態機能達成微秒級的絕對決定性，媲美工業級 PLC 標準。

## 4. 總結 (Conclusion)
發現系統極限並非工程失敗，而是架構演進的必然。透過明確界定 V4.2 的軟體防禦邊界與 V5.0 的物理 I/O 遷移藍圖，我們不僅建立了一個可驗證的安全閘道器原型，更刻畫了一條通往工業級量產的嚴謹技術路徑。