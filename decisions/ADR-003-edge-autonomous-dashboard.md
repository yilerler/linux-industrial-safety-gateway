# ADR-003: 邊緣自洽的戰情室與人機防呆機制 (Edge-Autonomous Dashboard & Human-in-the-Loop Fail-Safe)

## 1. 狀態 (Status)

已接受 (Accepted)

## 2. 背景與問題 (Context)

在確立了 IT/OT 雙軌解耦後，我們需要為廠務人員提供即時的系統可視化介面（Dashboard）。
最初評估整合 Firebase Cloud，將 Gateway 數據推播至雲端再由前端讀取。然而，工業現場存在以下致命風險：

* **WAN 依賴脆弱性 (Cloud Dependency)：** 若工廠對外網路斷線，或雲端服務器異常，現場戰情室將「失明」。
* **輪詢延遲 (Polling Latency)：** 傳統 HTTP 請求無法滿足硬即時系統的觀測需求，可能導致畫面狀態與物理狀態脫鉤。
* **人為恐慌干預 (Human Panic Intervention)：** 當 UI 顯示常見的 `Error 404` 或轉圈圈時，操作員大腦會直覺判定「機台失控」，進而做出錯誤的實體救援操作（如誤觸其他開關），引發二次工安事故。

## 3. 決策 (Decision)

我們決定捨棄外部雲端依賴，採用 **邊緣本地伺服 (Edge Local Hosting)** 與 **戰略前端 (Strategic UI)** 架構：

1. **閘道器伺服化：** 在 `adapter.js` 引入 Express.js，直接於 Edge Gateway 內網 (LAN) 提供靜態網頁託管。
2. **事件驅動推播：** 引入 WebSocket (`ws`) 建立雙向通道。Gateway 取得新數據後，主動推播 (Push) 給瀏覽器，將觀測延遲降至最低。
3. **優雅降級與 SOP 牧羊犬機制 (Graceful Degradation)：** 前端實作 Watchdog Timer（看門狗計時器）。若 3 秒未收到 WebSocket 心跳，畫面**不顯示紅色警報**，而是轉為冷靜的灰/黃色調。
   * 畫面凍結於「最後已知安全狀態 (Last Known Good State)」，並強制顯示 SOP 指引：「*IT 連線中斷。請勿靠近機台，底層 OT 實體防護仍獨立運作中。*」



## 4. 後果 (Consequences)

* **優點 (Positive):**
  * **邊緣自洽 (Edge Autonomy):** 達成零 WAN 網路依賴。只要廠區區域網路 (LAN) 正常，戰情室即可運作。
  * **心理防呆 (Psychological Fail-Safe):** 將 UI 從單純的「觀測器」，轉變為斷線時控制人類預期心理的「指揮官」，完美對齊人機迴圈 (Human-in-the-loop) 的安全設計。
  * **架構展示 (Architecture Proof):** 斷線降級的畫面，成為 IT/OT 解耦最直接的火力展示。

* **缺點/妥協 (Negative):**
  * 增加了 Edge Gateway 的運算負擔（`adapter.js` 需同時處理 ioctl、協定轉換與 WebSocket 連線管理），需密切監控 Node.js 的 Event Loop 阻塞狀況。
