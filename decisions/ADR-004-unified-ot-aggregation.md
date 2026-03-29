# ADR-004: 統一 OT 資料聚合於 Kernel 層 (Unified OT Data Aggregation at Kernel Layer)
## 1. 狀態 (Status)
已接受 (Accepted)

## 2. 背景與問題 (Context)
在早期的開發階段中，基於團隊分工與快速原型的需求（戰術性妥協），我們在 User Space 的 adapter.js 中實作了次要環境數據（如 PM2.5、噪音、RFID 門禁）的模擬與聚合邏輯。
然而，隨著系統演進為「工業級安全閘道器 (Edge Gateway)」，此架構暴露出嚴重的物理邏輯不自洽與越權問題：
* **越權的閘道器 (Architectural Mismatch)：** 邊緣閘道器的本質應為「網路路由器」與「通訊協定翻譯官」，絕不該在應用層自行產生或模擬 OT（營運技術）資料。
* **失真的數位孿生 (Distorted Digital Twin)：** 在真實場景中，所有感測器訊號皆來自底層的 PLC 或微控制器。若將環境數據的讀取綁定在 Node.js 中，將無法真實模擬 Gateway 向下發起 Modbus/SPI 輪詢的底層 I/O 行為。
* **中斷上下文的併發危險 (Interrupt Context Concurrency)：** 在重新檢視 Kernel 核心計時器 (mod_timer) 時，我們發現原先使用互斥鎖 (mutex) 來保護共享記憶體，在軟中斷上下文 (Softirq Context) 中存在極大的排程死結 (Kernel Panic) 風險。

## 3. 決策 (Decision)
為了達成真正的關注點分離 (Separation of Concerns, SoC) 並確立閘道器的純粹性，我們決定實施 **「OT 職責下沉 (OT Responsibility Sinking)」** 架構重構：

1. **建構底層暫存器映射表 (Unified Register Map)：**
將所有次要感測器的資料產生邏輯從 adapter.js 拔除，全面下放至 Kernel Space (mock_sensor.c)。擴充 ioctl 的通訊合約，使其行為等同於向底層設備讀取連續的 Modbus 暫存器區塊：

```bash
  C
// 擴張後的 sensor_ioctl.h (共 24 Bytes)
struct sensor_data {
    unsigned int timestamp;
    // --- 安全子系統 (OT 控制 / Coil & Holding Register) ---
    int distance_mm;
    int motor_status; // 0=NORMAL, 1=EMERGENCY_STOP
    // --- 環境與門禁子系統 (環境感測 / Input Register) ---
    int pm25;
    int noise_db;
    int rfid_card_id; // 0 代表 NO_CARD
} __attribute__((packed)); // ⚠️ 核心防禦：強制取消記憶體對齊填充
```
* **ABI 記憶體佈局防禦 (ABI Memory Layout Defense)：** 為了防止 C 語言編譯器自動插入空白的填充位元組 (Padding Bytes)，導致上層 Node.js 依靠固定 Offset 讀取 Buffer 時發生記憶體偏移 (Memory Misalignment)，我們強制加上 __attribute__((packed))，確保跨語言、跨環境的二進位介面 (ABI) 達到 100% 絕對一致。

2. **Middleware 純化 (Pure Translator)：**
adapter.js 不再包含任何 Math.random() 等業務邏輯，蛻變為純粹的「通訊中介層」。其唯一職責是透過 ioctl 讀取上述 24 Bytes 的二進位 Buffer，解碼並轉換為 JSON Payload 後，透過 WebSocket 向上推播。

3. **核心自旋鎖重構 (Spinlock Upgrade)：**
為配合更龐大的資料更新操作，並遵守中斷上下文「絕對不允許睡眠」的硬限制，將 mock_sensor.c 中的 mutex_lock 全面重構為中斷安全的 自旋鎖 (spin_lock_irqsave)，徹底防堵 D-State 殭屍行程與核心崩潰。

## 4. 後果 (Consequences)
* **優點 (Positive):**
  * **物理邏輯自洽 (Physical Consistency):** 系統架構對齊真實工業現場。Kernel Module 扮演向下輪詢的 Modbus Polling Engine，Node.js 扮演向上的 IT 橋接器。
  * **極致解耦 (Extreme Decoupling):** 由於嚴格遵守 API 邊界合約，此次底層資料來源的重大遷移，對前端戰情室 (dashboard.js) 達成了零程式碼修改 (Zero Code Change) 的完美無縫接軌。
  * **工安級防禦力提升 (Enhanced Safety):** 透過導入 spin_lock_irqsave，系統不僅解決了潛在的排程死結，更確保了馬達急停狀態機 (State Machine) 在高頻併發下的絕對記憶體一致性。
  * **跨語言 ABI 穩定性 (Cross-Language ABI Stability):** 透過 __attribute__((packed))，消除了 Kernel Space (C) 與 User Space (Node.js) 之間結構體解析的模糊地帶，確立了極度嚴謹的二進位通訊合約。

* **缺點/妥協 (Negative):**
  * **Kernel 開發複雜度增加:** 所有的感測器邏輯與硬體模擬都集中在 C 語言的 LKM 中，增加了除錯的困難度與核心記憶體佈局 (Memory Layout) 的維護成本。
