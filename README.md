# 🏭 Industrial IT/OT Safety Gateway (基於 Linux Kernel 之工業級安全閘道器)

> **一句話簡介：** 專為 **工業人機協作 (HRC)** 與 **高可靠度邊緣運算 (Edge Computing)** 設計的軟體定義閘道器 (Software-Defined Gateway) 與虛擬感測中樞 (Virtual Sensor Hub)。
> 透過分離 Linux Kernel Space (硬即時控制與邊緣濾波) 與 User Space (通訊協議轉換與戰略 UI)，解決傳統單一架構無法兼顧「IT 雲端大數據聚合」、「OT 底層極低延遲防護 (Fail-Safe)」以及「斷線時的人機防呆」的業界痛點。

## 🏗️ 系統架構 (System Architecture)

本專案採用三層式異質運算架構，展示「軟體定義硬體」、「IT/OT 解耦」與「邊緣自洽」的設計哲學：
```mermaid
flowchart TD
    %% Define Styles
    classDef hardware fill:#e0e0e0,stroke:#333,stroke-width:2px,color:#000
    classDef kernel fill:#f9d0c4,stroke:#e06666,stroke-width:2px,color:#000
    classDef user fill:#cfe2f3,stroke:#6fa8dc,stroke-width:2px,color:#000
    classDef external fill:#d9ead3,stroke:#93c47d,stroke-width:2px,color:#000
    classDef boundary fill:#fff2cc,stroke:#d6b656,stroke-width:2px,stroke-dasharray: 5 5,color:#000

    subgraph Physical["⚙️ 實體物理層 (Hardware / Environment)"]
        Sensors[感測器陣列<br>原始訊號 & 高頻雜訊]:::hardware
        Motor[馬達控制器<br>實體致動器]:::hardware
    end

    subgraph Kernel["🛡️ Linux 核心層 (Kernel Space - Hard Real-Time)"]
        DSP[DSP 訊號清洗<br>Moving Average Filter]:::kernel
        FailSafe[保命急停邏輯<br>Fail-Safe Engine]:::kernel
        
        Sensors -->|中斷 / 輪詢| DSP
        DSP -->|乾淨數據| FailSafe
        FailSafe -.->|絕對優先權: 強制斷電| Motor
    end

    subgraph Contract["⚖️ 系統邊界 (System Boundary)"]
        IOCTL((ioctl 合約<br>sensor_ioctl.h)):::boundary
    end

    subgraph User["🧠 應用業務層 (User Space - Node.js)"]
        Adapter[Edge Gateway<br>adapter.js]:::user
        Aggregator[次要數據聚合<br>空品 / 噪音 / 門禁]:::user
        Translator[通訊協議翻譯官<br>Protocol Translator]:::user
        WSServer[本地伺服器<br>Express + WebSocket]:::user

        FailSafe -->|狀態提取| IOCTL
        IOCTL --> Adapter
        Adapter <--> Aggregator
        Adapter --> Translator
        Adapter --> WSServer
    end

    subgraph External["🌍 外部系統與人機介面 (External & HMI)"]
        IT[☁️ 雲端系統<br>IT System]:::external
        OT[📠 傳統控制器<br>OT System / PLC]:::external
        UI[💻 戰情室 UI<br>Graceful Degradation]:::external

        Translator -->|JSON 大數據<br>~190 Bytes| IT
        Translator -->|Hex + Checksum<br>6 Bytes| OT
        WSServer <-->|LAN 即時推播<br>Watchdog 監控| UI
    end
  ```  
## ✨ 核心工程價值 (Key Features)

* 🛡️ **工安級隔離 (Safety-Critical Isolation):** 保命邏輯直接在 Kernel Timer 內反射觸發，實作零延遲的硬體防護。
* ⏱️ **確定性採樣與防暴走 (Deterministic & State Machine):** 擺脫 OS 排程帶來的 Jitter，確保底層每 100ms 絕對執行；內建狀態機 (State Machine) 抑制 Log 風暴。
* 🌐 **邊緣自洽戰情室 (Edge-Autonomous Dashboard):** 內建 Express 與 WebSocket 伺服器，達成零 WAN 依賴的區域網路即時可視化。
* 🚨 **優雅降級與心理防呆 (Graceful Degradation):** 前端實作 Watchdog 看門狗，斷線時自動凍結畫面並切換為「SOP 指揮官模式」，防止人員恐慌誤操作 (Human-in-the-Loop 安全設計)。
* 📉 **核心級訊號清洗 (Kernel-Space DSP):** 在 Linux 驅動底層實作滑動平均濾波器，於物理雜訊進入 User Space 前即時抑制，確保防護邊界的絕對穩定。
* 🔀 **IT/OT 雙軌通訊 (Dual-Track Telemetry):** 閘道器向上發布雲端友善的 JSON 負載，向下則針對傳統控制器壓制出僅 6 Bytes 且含校驗碼 (Checksum) 的工業級 Hex 封包。
* 🔒 **防禦性工程 (Defensive Engineering):** 嚴格的 Mutex 鎖控管防止 D-State 死鎖；內建 SIGINT 優雅退出機制 (Graceful Shutdown)，徹底消滅殭屍行程與 Socket 資源競態。

## 📂 專案結構 (Directory Structure)

```text
.
├── decisions/          # 架構決策與事後剖析 (ADR & Postmortem)
├── kernel/             # Linux LKM 驅動原始碼
│   ├── include/        # 跨層共享的 IOCTL 合約定義
│   └── src/            # mock_sensor.c (保命機制、狀態機與虛擬硬體)
└── user/               # Node.js 邊緣運算層
    ├── public/         # Vanilla JS/CSS 打造之高對比工業戰情室 UI
    └── adapter.js      # 系統資料聚合、API 轉發與 Web 伺服器
```

## 🚀 系統輸出展示 (IT/OT 解耦架構)

本閘道器無縫橋接了雲端 (IT) 與工廠現場 (OT) 的通訊鴻溝，於終端機即時展示兩種截然不同的資料流聚合結果：

```text
======================================================
☁️  [IT-Layer] Cloud JSON Payload (190 Bytes)
{
  timestamp: '2026-02-21T18:15:43.770Z',
  safety_subsystem: { distance_mm: 193, status: 'NORMAL' },
  environment_subsystem: { pm25: 32, noise_db: 85 },
  access_subsystem: { last_scan: 'NO_CARD' }
}
⚙️  [OT-Layer] Industrial Hex Payload (6 Bytes)
📡 UART TX -> [ 0xAA 0x01 0x00 0xC1 0x00 0x6C ]
======================================================
```

## 🚀 快速啟動 (Getting Started)

**1. 編譯與載入核心模組 (Kernel Space)**
```bash
cd kernel
make
sudo insmod src/mock_sensor.ko
dmesg | tail # 驗證驅動是否存活
```

**2. 啟動邊緣聚合器 (User Space)**
```bash
cd user
npm install
sudo node adapter.js # 備註：此處需 sudo 權限以存取 /dev/mock_sensor 字元設備 (Character Device)。量產環境將透過 udev rules 配置 user group 權限以符合 Least Privilege (最小權限) 原則。
```

## 📜 架構決策紀錄 (ADRs)
詳細的系統設計與技術選型考量，請參閱：
* [ADR-001: 針對安全關鍵邊緣系統的混合架構](decisions/ADR-001-hybrid-architecture.md)
* [ADR-002: 閘道器中的 IT/OT 雙軌通訊協定轉換](./decisions/ADR-002-it-ot-protocol-translation.md)
* [ADR-003: 邊緣自洽的戰情室與人機防呆機制](decisions/ADR-003-edge-autonomous-dashboard.md)
* [POSTMORTEM-001: 系統規模對齊與 Kernel Deadlock 事件](decisions/POSTMORTEM-001-kernel-deadlock.md)
