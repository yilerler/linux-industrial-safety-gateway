#ifndef SENSOR_IOCTL_H
#define SENSOR_IOCTL_H

#include <linux/ioctl.h>

/* --- 定義 OT 狀態常數 (Coils & Status) --- */
#define STATUS_NORMAL 0
#define STATUS_EMERGENCY_STOP 1

/* * 🛡️ 跨層資料合約 (Unified Register Map) 
 * * 【架構防禦機制】：
 * 使用 __attribute__((packed)) 強制編譯器取消記憶體對齊填充 (Memory Padding)。
 * 這能確保 C 結構體的記憶體佈局與 Node.js 依靠 Byte Offset 讀取的 Buffer
 * 達到 100% 的絕對一致，徹底消滅 ABI (應用二進位介面) 不相容的崩潰風險。
 */
struct sensor_data {
    unsigned int timestamp;   // Offset: 0 bytes  (System Tick)
    
    // --- 安全子系統 (OT 控制 / 模擬 Holding Register) ---
    int distance_mm;          // Offset: 4 bytes  (雷達測距)
    int motor_status;         // Offset: 8 bytes  (0=NORMAL, 1=EMERGENCY_STOP)
    
    // --- 環境與門禁子系統 (環境感測 / 模擬 Input Register) ---
    int pm25;                 // Offset: 12 bytes (空氣品質)
    int noise_db;             // Offset: 16 bytes (環境噪音)
    int rfid_card_id;         // Offset: 20 bytes (0 代表 NO_CARD)
} __attribute__((packed));    // <--- 價值連城的核彈級防禦機制！

/* --- 定義 IOCTL 系統呼叫 (System Call Magic) --- */
#define SENSOR_MAGIC 'S'

/* * 註：_IOR 的第三個參數會自動計算 struct sensor_data 的大小。
 * 因為我們加了 packed，這裡的大小會是精準的 24 Bytes (6 個 int * 4 bytes)。
 */
#define IOCTL_GET_DATA _IOR(SENSOR_MAGIC, 1, struct sensor_data)

// 保留作為擴充使用 (例如未來由 IT 下達 Reset 指令)
#define IOCTL_SET_MOCK_DISTANCE _IOW(SENSOR_MAGIC, 2, int)

#endif