#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>

static int
lcore_hello(__rte_unused void *arg) {
    unsigned lcore_id;
    lcore_id = rte_lcore_id();  // Get the current core ID
    printf("hello from core %u\n", lcore_id);
    return 0;
}

int main(int argc, char **argv) {
    int ret;
    unsigned lcore_id;

    // 初始化 EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");
    rte_log_set_level(RTE_LOGTYPE_EAL, RTE_LOG_DEBUG);
    
    // 在每个 Worker 核心上异步启动task
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        printf("worker lcore id: %d\n", lcore_id);
        rte_eal_remote_launch(lcore_hello, NULL, lcore_id);
    }

    // main 核心自己也执行一遍
    lcore_hello(NULL);

    // 等待所有 worker 核心完成
    rte_eal_mp_wait_lcore();

    // 清理 EAL
    rte_eal_cleanup();

    return 0;
} 