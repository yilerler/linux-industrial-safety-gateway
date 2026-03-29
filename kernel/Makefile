# 1. 定義要編譯的物件
# obj-m 意思是把這個物件編成 module (不是編成核心的一部分)
# 注意路徑：我們的 .c 檔在 src/ 資料夾裡
obj-m += src/mock_elc_core.o

# 2. 定義核心原始碼的位置
# uname -r 會自動抓你現在 Debian 的版本 (例如 6.1.0-rpi4)
KERN_DIR = /lib/modules/$(shell uname -r)/build

# 3. 定義編譯目標 (all)
# make -C $(KERN_DIR): 切換到核心原始碼目錄，借用它的 Makefile
# M=$(PWD): 告訴核心，「借用完工具後，請回到我現在這個目錄來編譯我的 Code」
# modules: 這是核心 Makefile 裡定義好的任務，專門用來編譯模組
all:
	make -C $(KERN_DIR) M=$(PWD) modules

# 4. 定義清理目標 (clean)
# 把編譯出來的垃圾 (.o, .ko, .mod.c ...) 全部清掉
clean:
	make -C $(KERN_DIR) M=$(PWD) clean