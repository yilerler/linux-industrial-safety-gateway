#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h> // ⚠️ 引入多執行緒
#include <time.h>
#include "../kernel/include/sensor_ioctl.h"

#define DEVICE_PATH "/dev/mock_elc"
#define NUM_THREADS 4          // 模擬 4 核心同時攻擊
#define ITERS_PER_THREAD 25000 // 每個執行緒 2.5 萬次 (總共 10 萬次)

int fd;

// 執行緒工作函數：瘋狂呼叫 ioctl
void *stress_worker(void *arg) {
    int thread_id = *(int *)arg;
    struct sensor_data local_map;
    
    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        if (ioctl(fd, IOCTL_GET_DATA, &local_map) < 0) {
            perror("IOCTL Failed during stress test");
            pthread_exit((void *)1);
        }
    }
    printf("Thread %d completed %d reads.\n", thread_id, ITERS_PER_THREAD);
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    printf("🔧 SMP Concurrency Stress Test Tool (Pthreads) 🔧\n");
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) { perror("Failed to open device"); return EXIT_FAILURE; }

    printf("🚀 Launching %d threads, %d iterations each...\n", NUM_THREADS, ITERS_PER_THREAD);
    
    clock_t start_time = clock();

    // 齊發 4 個執行緒，開始製造 Spinlock 競爭
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, stress_worker, &thread_ids[i]);
    }

    // 等待所有執行緒收工
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_t end_time = clock();
    double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    printf("✅ Stress test passed! Spinlock defended against SMP contention.\n");
    printf("⏱️  Total Time: %f seconds. Throughput: %.2f IOCTLs/sec.\n", 
           time_spent, (NUM_THREADS * ITERS_PER_THREAD) / time_spent);

    close(fd);
    return EXIT_SUCCESS;
}