# 🚀 Embedded Hybrid Architecture Scaffold (嵌入式異質架構整合骨架)

> **一句話簡介：** 專為高可靠度 IoT 邊緣閘道器 (Edge Gateway) 設計的系統骨架。透過分離 Linux Kernel Space (硬即時控制) 與 User Space (業務邏輯)，解決傳統單一腳本控制硬體時缺乏確定性 (Determinism) 與故障隔離 (Fail-Safe) 的致命痛點。

## 🏗️ 系統架構 (System Architecture)

本專案採用三層式異質運算架構：

1. **L1 / 核心控制層 (Kernel Space - C 語言):**
   * 負責硬即時任務（如：電子圍籬感測、馬達強制斷電）。
   * 具備 **Fail-Safe** 獨立運作能力，不受上層系統負載影響。
2. **L2 / 業務邏輯層 (User Space - Node.js):**
   * 透過 `ioctl` 介面與底層通訊。
   * 負責非即時任務（如：空品/噪音監測、門禁 RFID 驗證）。
   * 擔任資料聚合器 (Data Aggregator)，打包系統狀態。
3. **L3 / 雲端戰情層 (Cloud Space):**
   * 接收結構化的 JSON Payload，渲染監控儀表板。

## ✨ 核心工程價值 (Key Features)

* 🛡️ **工安級隔離 (Safety-Critical Isolation):** 保命邏輯直接在 Kernel Timer 內反射觸發，實作零延遲的硬體防護。
* ⏱️ **確定性採樣 (Deterministic Sampling):** 擺脫 OS 排程帶來的 Jitter，確保底層每 100ms 絕對執行一次感測。
* 🔌 **軟體定義硬體 (Software-Defined Hardware):** 嚴格定義 `sensor_ioctl.h` 合約，使邏輯層與物理硬體完全脫鉤。
* 🚀 **虛擬化開發 (Mock-Driven Development):** 內建硬體模擬器，無須連接真實硬體即可進行架構驗證。
* 📉 **核心級訊號清洗 (Kernel-Space DSP):** 在 Linux 驅動底層實作滑動平均濾波器 (Moving Average Filter)，於物理雜訊進入 User Space 前即時抑制，確保防護邊界的絕對穩定。
* 🔀 **IT/OT 雙軌通訊 (Dual-Track Telemetry):** 閘道器兼具協議翻譯能力，向上發布雲端友善的 JSON 負載，向下則針對傳統控制器 (PLC/MCU) 壓制出僅 6 Bytes 且含校驗碼 (Checksum) 的工業級 Hex 封包。

## 📂 專案結構 (Directory Structure)

```text
.
├── decisions/          # 架構決策紀錄 (ADR)
├── kernel/             # Linux LKM 驅動原始碼
│   ├── include/        # 跨層共享的 IOCTL 合約定義
│   └── src/            # mock_sensor.c (保命機制與虛擬硬體)
└── user/               # Node.js 邊緣運算層
    └── adapter.js      # 系統資料聚合與 API 轉發
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
sudo node adapter.js
```

## 📜 架構決策紀錄 (ADRs)
詳細的系統設計與技術選型考量，請參閱：
* [ADR-001: 針對安全關鍵邊緣系統的混合架構](decisions/ADR-001-hybrid-architecture.md)
* [ADR-002: 閘道器中的 IT/OT 雙軌通訊協定轉換](./decisions/ADR-002-it-ot-protocol-translation.md)