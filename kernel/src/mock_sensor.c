#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/device.h>  // 新增：為了 class 和 device
#include <linux/err.h>     // 新增：為了處理錯誤指標
#include "../include/sensor_ioctl.h"

#define DEVICE_NAME "mock_sensor"
#define CLASS_NAME  "mock_class" // 新增：裝置類別名稱
#define FILTER_WINDOW_SIZE 5  // 滑動平均濾波器的視窗大小 (取最近5次平均)

struct mock_sensor_dev {
    struct cdev cdev;
    struct mutex lock;
    struct timer_list timer;
    struct sensor_data data;
    int is_active;
    struct class *dev_class;
    struct device *dev_device;
    int direction; // 新增：0 = 遠離中, 1 = 靠近中
    int filter_history[FILTER_WINDOW_SIZE]; // 儲存最近 N 次的原始數據
    int filter_index;                       // 環形緩衝區的當前指標
};

static dev_t dev_num;
static struct mock_sensor_dev *my_dev;

// 🌊 工業級滑動平均濾波器 (Moving Average Filter)
static int apply_moving_average(struct mock_sensor_dev *dev, int raw_val)
{
    int i;
    long sum = 0;

    // 1. 將新數據寫入環形緩衝區
    dev->filter_history[dev->filter_index] = raw_val;

    // 2. 更新指標位置
    dev->filter_index = (dev->filter_index + 1) % FILTER_WINDOW_SIZE;

    // 3. 計算平均值
    for (i = 0; i < FILTER_WINDOW_SIZE; i++) {
        sum += dev->filter_history[i];
    }

    return (int)(sum / FILTER_WINDOW_SIZE);
}

// --- Timer Function ---
static void mock_hardware_timer_func(struct timer_list *t) {
    struct mock_sensor_dev *dev = from_timer(dev, t, timer);
    int noise;
    
    // 新增：用來記錄「物理世界」的真實原始距離，而不是直接改 dev->data.distance_mm
    static int raw_physical_distance = 100; 
    int filtered_distance;

    mutex_lock(&dev->lock);
    
    // 產生一點雜訊 (放大雜訊比例，讓 UI 上的數字跳動更真實)
    noise = (int)(jiffies % 5) * 10;

    // --- 1. 物理現象模擬邏輯 (產生 Raw Data) ---
    if (dev->direction == 0) { 
        // 遠離中 (每 0.1 秒移動約 5~9 公分)
        raw_physical_distance += (50 + noise);
        if (raw_physical_distance >= 3500) { // 上限改為 3.5 公尺 (3500 mm)
            raw_physical_distance = 3500;
            dev->direction = 1; // 折返
        }
    } else {
        // 靠近中
        raw_physical_distance -= (50 + noise);
        if (raw_physical_distance <= 500) {  // 下限改為 0.5 公尺 (500 mm)，讓它一定會衝過紅線
            raw_physical_distance = 500;
            dev->direction = 0; // 折返
        }
    }

    // --- 2. 核心：經過滑動平均濾波器清洗數據 ---
    filtered_distance = apply_moving_average(dev, raw_physical_distance);
    dev->data.distance_mm = filtered_distance;

    // --- 3. 保命急停邏輯 (基於清洗後的數據) ---
    // ⚠️ 工業級紅線：小於 1000 mm (1 公尺) 強制斷電！
    if (dev->data.distance_mm < 1000) {
        dev->data.status_code = STATUS_EMERGENCY_STOP;
        printk(KERN_EMERG "Mock Sensor: [SAFETY CRITICAL] Distance < 1000mm! MOTOR STOPPED!\n");
    } else {
        dev->data.status_code = STATUS_NORMAL;
    }
}

// --- IOCTL Function ---
static long mock_sensor_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct mock_sensor_dev *dev = file->private_data;
    int ret = 0;
    int new_dist;

    mutex_lock(&dev->lock);
    switch (cmd) {
        case IOCTL_GET_DATA:
            if (copy_to_user((struct sensor_data *)arg, &dev->data, sizeof(struct sensor_data))) {
                ret = -EFAULT;
            }
            break;
            
        // 新增：手動設定距離 (這是為了測試！)
        case IOCTL_SET_MOCK_DISTANCE:
            if (copy_from_user(&new_dist, (int *)arg, sizeof(int))) {
                ret = -EFAULT;
            } else {
                dev->data.distance_mm = new_dist;
                printk(KERN_INFO "Mock Sensor: Manual distance set to %dmm\n", new_dist);
            }
            break;

        default:
            ret = -EINVAL;
    }
    mutex_unlock(&dev->lock);
    return ret;
}

static int mock_sensor_open(struct inode *inode, struct file *file) {
    struct mock_sensor_dev *dev = container_of(inode->i_cdev, struct mock_sensor_dev, cdev);
    file->private_data = dev;
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = mock_sensor_open,
    .unlocked_ioctl = mock_sensor_ioctl,
};

// --- Init ---
static int __init mock_sensor_init(void) {
    int ret;
    
    // 1. 申請裝置編號
    if ((ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME)) < 0) {
        return ret;
    }

    my_dev = kzalloc(sizeof(struct mock_sensor_dev), GFP_KERNEL);
    if (!my_dev) {
        unregister_chrdev_region(dev_num, 1);
        return -ENOMEM;
    }

    memset(my_dev->filter_history, 0, sizeof(my_dev->filter_history));
    my_dev->filter_index = 0;
    
    mutex_init(&my_dev->lock);
    timer_setup(&my_dev->timer, mock_hardware_timer_func, 0);

    cdev_init(&my_dev->cdev, &fops);
    if ((ret = cdev_add(&my_dev->cdev, dev_num, 1)) < 0) {
        kfree(my_dev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }

    // 2. 新增：自動建立 /dev/mock_sensor 節點
    // 這會通知 udev 在 /dev 下建立檔案
    my_dev->dev_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(my_dev->dev_class)) {
        cdev_del(&my_dev->cdev);
        kfree(my_dev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(my_dev->dev_class);
    }

    my_dev->dev_device = device_create(my_dev->dev_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(my_dev->dev_device)) {
        class_destroy(my_dev->dev_class);
        cdev_del(&my_dev->cdev);
        kfree(my_dev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(my_dev->dev_device);
    }

    // 3. 啟動 Timer
    my_dev->is_active = 1;
    my_dev->direction = 0; // 預設往外跑
    my_dev->data.distance_mm = 100;
    mod_timer(&my_dev->timer, jiffies + msecs_to_jiffies(100));

    printk(KERN_INFO "Mock Sensor: Initialized successfully with /dev/%s.\n", DEVICE_NAME);
    return 0;
}

// --- Exit ---
static void __exit mock_sensor_exit(void) {
    del_timer_sync(&my_dev->timer); // 使用 sync 確保 timer 真的停了
    
    // 新增：移除裝置節點和類別
    device_destroy(my_dev->dev_class, dev_num);
    class_destroy(my_dev->dev_class);

    cdev_del(&my_dev->cdev);
    kfree(my_dev);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "Mock Sensor: Goodbye.\n");
}

module_init(mock_sensor_init);
module_exit(mock_sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joshua");
MODULE_DESCRIPTION("A Mock Sensor Driver for Hybrid Architecture Scaffold");