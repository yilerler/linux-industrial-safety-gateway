#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/spinlock.h>  // 🛡️ 引入自旋鎖
#include <linux/random.h>    // 🎲 引入亂數產生器 (模擬真實 I/O 數據)
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "../include/sensor_ioctl.h"

#define DEVICE_NAME "mock_elc"
#define CLASS_NAME "elc_class"
#define FILTER_WINDOW_SIZE 5

/* * 🗄️ 邊緣邏輯控制器 (ELC) 核心設備結構體
 * 掌管全局狀態、暫存器地圖與自旋鎖
 */
struct elc_device {
    struct cdev cdev;
    spinlock_t lock;             // 🛡️ 核心防禦：中斷安全的自旋鎖
    struct timer_list timer;
    struct sensor_data reg_map;  // 🗄️ 統一暫存器映射表 (Register Map)
    
    int is_active;
    struct class *dev_class;
    struct device *dev_device;

    // 運動學物理模擬狀態 (專屬此 Mock 模組使用)
    int raw_physical_distance;
    int direction; 
    int filter_history[FILTER_WINDOW_SIZE];
    int filter_index;
};

static dev_t dev_num;
static struct elc_device *my_dev;

/* =========================================================================
 * ⬇️ Layer 1: 南向硬體抽象層 (Southbound HAL)
 * 職責：與實體物理世界溝通。未來若替換真實感測器，只需修改此層 API。
 * ========================================================================= */
static int hal_read_raw_distance(struct elc_device *dev) {
    int noise = (int)(get_random_u32() % 50); // 模擬高頻電磁雜訊
    if (dev->direction == 0) { 
        dev->raw_physical_distance += (50 + noise);
        if (dev->raw_physical_distance >= 3500) dev->direction = 1;
    } else {
        dev->raw_physical_distance -= (50 + noise);
        if (dev->raw_physical_distance <= 500) dev->direction = 0;
    }
    return dev->raw_physical_distance;
}

static int hal_read_pm25(void) { return 10 + (get_random_u32() % 40); }
static int hal_read_noise(void) { return 40 + (get_random_u32() % 50); }
static int hal_read_rfid(void) { 
    // 10% 機率刷卡成功，返回一組卡號；否則返回 0 (NO_CARD)
    return (get_random_u32() % 100 > 90) ? (get_random_u32() % 10000) : 0; 
}

/* =========================================================================
 * 🧠 Layer 2: ELC 核心輪詢與狀態機層 (Modbus Engine & State Machine)
 * 職責：執行於 Softirq 中斷上下文，負責清洗資料、判斷工安邏輯並更新暫存器。
 * ========================================================================= */
static void elc_core_timer_func(struct timer_list *t) {
    struct elc_device *dev = from_timer(dev, t, timer);
    unsigned long flags;
    int raw_dist, clean_dist, i;
    long sum = 0;

    // 1. 呼叫 HAL 獲取物理數據 (依賴反轉，解耦硬體)
    raw_dist = hal_read_raw_distance(dev);

    // 2. 核心邊緣運算：DSP 滑動平均濾波 (清洗雜訊)
    dev->filter_history[dev->filter_index] = raw_dist;
    dev->filter_index = (dev->filter_index + 1) % FILTER_WINDOW_SIZE;
    for (i = 0; i < FILTER_WINDOW_SIZE; i++) sum += dev->filter_history[i];
    clean_dist = (int)(sum / FILTER_WINDOW_SIZE);

    /* ---------------------------------------------------------
     * 🔒 進入中斷安全禁區 (取得 Spinlock 並暫存/關閉本地 CPU 中斷)
     * --------------------------------------------------------- */
    spin_lock_irqsave(&dev->lock, flags);

    // 3. 更新統一暫存器地圖 (Unified Register Map)
    dev->reg_map.timestamp = jiffies;
    dev->reg_map.distance_mm = clean_dist;
    dev->reg_map.pm25 = hal_read_pm25();
    dev->reg_map.noise_db = hal_read_noise();
    
    // RFID 只在有卡片時才覆寫，模擬硬體中斷暫存器行為
    int new_rfid = hal_read_rfid();
    if (new_rfid > 0) dev->reg_map.rfid_card_id = new_rfid;

    // 4. 工安急停狀態機 (Hardware Interlock)
    if (clean_dist < 1000) {
        if (dev->reg_map.motor_status != STATUS_EMERGENCY_STOP) {
            printk(KERN_EMERG "[ELC Core] CRITICAL: Distance < 1000mm. Motor Interlock Triggered!\n");
            dev->reg_map.motor_status = STATUS_EMERGENCY_STOP;
            // 未來可在此呼叫：hal_trigger_relay_cut(); 實體切斷電源
        }
    } else {
        if (dev->reg_map.motor_status != STATUS_NORMAL) {
            printk(KERN_INFO "[ELC Core] SAFE: Distance restored. Motor Ready.\n");
            dev->reg_map.motor_status = STATUS_NORMAL;
            // 清除過期的 RFID 卡號，表示系統已恢復安全初始狀態
            dev->reg_map.rfid_card_id = 0; 
        }
    }

    /* ---------------------------------------------------------
     * 🔓 解除禁區 (釋放 Spinlock 並恢復中斷狀態)
     * --------------------------------------------------------- */
    spin_unlock_irqrestore(&dev->lock, flags);

    // 重新啟動 100ms 硬即時輪詢
    if (dev->is_active) mod_timer(&dev->timer, jiffies + msecs_to_jiffies(100));
}

/* =========================================================================
 * ⬆️ Layer 3: 北向通訊介面層 (Northbound Interface)
 * 職責：處理 Node.js 下達的 ioctl 請求，嚴格防禦 copy_to_user 睡眠陷阱。
 * ========================================================================= */
static long elc_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct elc_device *dev = file->private_data;
    
    // ⚠️ 關鍵防禦：宣告在 Stack 上的 24 Bytes 區域變數 (極度安全，無溢位風險)
    struct sensor_data local_copy; 
    unsigned long flags;
    int ret = 0;

    switch (cmd) {
        case IOCTL_GET_DATA:
            // 1. 🔒 快速鎖定，只做 O(1) 的記憶體深拷貝 (Deep Copy)
            spin_lock_irqsave(&dev->lock, flags);
            local_copy = dev->reg_map; 
            spin_unlock_irqrestore(&dev->lock, flags);
            
            // 2. 🔓 解鎖後，才執行危險且可能觸發 Page Fault 睡眠的 copy_to_user
            if (copy_to_user((struct sensor_data *)arg, &local_copy, sizeof(struct sensor_data))) {
                ret = -EFAULT;
            }
            break;
            
        case IOCTL_SET_MOCK_DISTANCE:
            // 保留給未來的故障注入 (Fault Injection) 測試
            break;

        default:
            ret = -EINVAL;
    }
    return ret;
}

static int elc_open(struct inode *inode, struct file *file) {
    struct elc_device *dev = container_of(inode->i_cdev, struct elc_device, cdev);
    file->private_data = dev;
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = elc_open,
    .unlocked_ioctl = elc_ioctl,
};

/* =========================================================================
 * 生命週期管理 (Init & Exit)
 * ========================================================================= */
static int __init elc_init(void) {
    int ret;
    if ((ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME)) < 0) return ret;

    // 於 Kernel Heap 動態配置主體結構，避免 Stack Overflow
    my_dev = kzalloc(sizeof(struct elc_device), GFP_KERNEL);
    if (!my_dev) {
        unregister_chrdev_region(dev_num, 1);
        return -ENOMEM;
    }

    spin_lock_init(&my_dev->lock); // 初始化自旋鎖
    timer_setup(&my_dev->timer, elc_core_timer_func, 0);

    cdev_init(&my_dev->cdev, &fops);
    if ((ret = cdev_add(&my_dev->cdev, dev_num, 1)) < 0) goto err_cdev;

    // 建立 Device Class (相容新舊版 Kernel API)
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
        my_dev->dev_class = class_create(CLASS_NAME);
    #else
        my_dev->dev_class = class_create(THIS_MODULE, CLASS_NAME);
    #endif
    
    if (IS_ERR(my_dev->dev_class)) { ret = PTR_ERR(my_dev->dev_class); goto err_class; }

    my_dev->dev_device = device_create(my_dev->dev_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(my_dev->dev_device)) { ret = PTR_ERR(my_dev->dev_device); goto err_device; }

    // 初始化狀態機與物理模擬
    my_dev->is_active = 1;
    my_dev->raw_physical_distance = 1000;
    my_dev->reg_map.motor_status = STATUS_NORMAL;
    mod_timer(&my_dev->timer, jiffies + msecs_to_jiffies(100));

    printk(KERN_INFO "ELC Core: Initialized successfully with /dev/%s.\n", DEVICE_NAME);
    return 0;

err_device:
    class_destroy(my_dev->dev_class);
err_class:
    cdev_del(&my_dev->cdev);
err_cdev:
    kfree(my_dev);
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

static void __exit elc_exit(void) {
    my_dev->is_active = 0;
    del_timer_sync(&my_dev->timer); // 確保 Timer 徹底停歇
    device_destroy(my_dev->dev_class, dev_num);
    class_destroy(my_dev->dev_class);
    cdev_del(&my_dev->cdev);
    kfree(my_dev);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "ELC Core: Goodbye.\n");
}

module_init(elc_init);
module_exit(elc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joshua");
MODULE_DESCRIPTION("Edge Logic Controller (ELC) Modbus Polling Engine & Hardware Interlock");