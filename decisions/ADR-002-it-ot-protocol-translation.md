# ADR-002: 閘道器中的 IT/OT 雙軌通訊協定轉換 (Dual-Track Protocol Translation)

## 1. 狀態 (Status)
已接受 (Accepted)

## 2. 背景 (Context)
在工業自動化 (Industry 4.0) 的場景中，銜接資訊技術 (IT) 與營運技術 (OT) 是一大系統架構挑戰。
* **IT 系統 (雲端戰情室)：** 依賴高頻寬網路 (如 Wi-Fi/Ethernet)，並期望接收具備自我描述性、高可讀性的結構化資料 (如 JSON)。
* **OT 系統 (PLC、微控制器、馬達驅動器)：** 運作於資源極度受限的環境 (RAM 極小、依賴 UART/RS-485 低頻寬傳輸)。它們需要嚴格、確定性的二進位負載 (Binary Payload)，以確保微秒級的反應速度。

若將 JSON 字串直接下發至傳統 OT 硬體，將導致緩衝區溢位 (Buffer Overflow)、過高的字串解析延遲，且缺乏對抗工廠電磁干擾 (EMI) 的資料完整性驗證機制。

## 3. 決策 (Decision)
我們決定在 User Space 的 `adapter.js` 閘道器中，實作 **雙軌序列化策略 (Dual-Track Serialization Strategy)**：
1. **IT 軌道：** 將系統狀態（工安距離、環境數據、門禁狀態）聚合成標準的 JSON Payload，供上層應用拉取。
2. **OT 軌道：** 將攸關人命的安全狀態，即時翻譯為極簡的 **工業十六進位協定 (Hex Protocol)**。該封包嚴格限制為 6 Bytes：
    * `Byte 0:` 標頭檔 Header (`0xAA`)
    * `Byte 1:` 指令碼 Command ID (`0x01` 代表安全狀態回報)
    * `Byte 2-3:` 距離數據 Distance，採用 16-bit Big Endian 格式。
    * `Byte 4:` 狀態碼 Status (`0x00` 正常, `0x01` 緊急停止)。
    * `Byte 5:` 檢查碼 Checksum (前五個 Byte 加總取 8-bit)，確保物理傳輸的資料正確性。

## 4. 後果 (Consequences)
* **優勢 (Positive):** 達成真正的 IT/OT 解耦。單一閘道器能同時滿足「雲端大數據分析」與「底層硬體極低延遲控制」的雙重極端需求。
* **優勢 (Positive):** 透過 Checksum 演算法，大幅提升了系統在惡劣工業環境中的容錯率。
* **劣勢 (Negative):** 未來若要在 OT 軌道新增感測器數據，必須重新設計嚴格的 Byte-offset 協定格式，擴展性不如 JSON 彈性。
