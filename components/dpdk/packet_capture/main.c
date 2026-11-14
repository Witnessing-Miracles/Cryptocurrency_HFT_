#include <stdio.h>
#include <stdint.h>
#include <signal.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ether.h>

#define NUM_MBUFS       8191
#define MBUF_CACHE_SIZE 250
#define RX_RING_SIZE    1024
#define MAX_PKT_BURST   32

// 网卡寄存器经常用到
// 这个变量可能随时被外部改变，每次用它时，都要重新去读取，编译器不能一直使用之前读到的值
static volatile sig_atomic_t force_quit;
static struct rte_mempool *mbuf_pool;
static uint64_t total_packets;
static uint64_t total_bytes;

static void signal_handler(int signum)
{
    (void)signum;
    force_quit = 1;
}

/* 处理收到的数据包；这里只打印长度并累计统计。 */
static void process_packet(struct rte_mbuf *pkt)
{
    printf("received packet: %u bytes\n", pkt->pkt_len);
    total_packets++;
    total_bytes += pkt->pkt_len;
}

/* 初始化端口：配置一个 RX 队列，不配置 TX 队列。 */
static int port_init(uint16_t port_id)
{
    struct rte_eth_conf port_conf = {0};
    uint16_t nb_rx_desc = RX_RING_SIZE;
    int ret;

    ret = rte_eth_dev_configure(port_id, 1, 0, &port_conf);
    if (ret != 0)
        return ret;

    ret = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rx_desc, NULL);
    if (ret != 0)
        return ret;

    ret = rte_eth_rx_queue_setup(port_id, 0, nb_rx_desc,
                                 rte_eth_dev_socket_id(port_id),
                                 NULL, mbuf_pool);
    if (ret != 0)
        return ret;

    ret = rte_eth_dev_start(port_id);
    if (ret != 0)
        return ret;

    /* 混杂模式是可选项。 */
    rte_eth_promiscuous_enable(port_id);
    printf("Port %u started\n", port_id);
    return 0;
}

/* 从每个端口的 RX queue 0 批量收包。 */
static void capture_loop(void)
{
    uint16_t port_id;
    struct rte_mbuf *pkts[MAX_PKT_BURST];

    while (!force_quit) {
        RTE_ETH_FOREACH_DEV(port_id) {
            uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, pkts,
                                              MAX_PKT_BURST);
            for (uint16_t i = 0; i < nb_rx; i++) {
                process_packet(pkts[i]);
                rte_pktmbuf_free(pkts[i]);
            }
        }
    }
}

int main(int argc, char **argv)
{
    int ret;
    uint16_t port_id;
    uint16_t nb_ports;

    /* 1. 初始化 EAL。 */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "EAL initialization failed\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 2. 检查可用端口。 */
    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");

    /* 3. 创建接收数据包用的 mbuf 内存池。 */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL",
                                        NUM_MBUFS * nb_ports,
                                        MBUF_CACHE_SIZE,
                                        0,
                                        RTE_MBUF_DEFAULT_BUF_SIZE,
                                        rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    /* 4. 配置并启动所有端口。 */
    RTE_ETH_FOREACH_DEV(port_id) {
        ret = port_init(port_id);
        if (ret != 0)
            rte_exit(EXIT_FAILURE, "Cannot initialize port %u\n", port_id);
    }

    /* 5. 收包，按 Ctrl+C 退出。 */
    printf("Capturing packets. Press Ctrl+C to stop.\n");
    capture_loop();

    /* 6. 停止端口并关闭。 */
    RTE_ETH_FOREACH_DEV(port_id) {
        rte_eth_dev_stop(port_id);
        rte_eth_dev_close(port_id);
    }

    printf("Packets: %lu, bytes: %lu\n",
           (unsigned long)total_packets, (unsigned long)total_bytes);

    /* 7. 清理 EAL。 */
    rte_eal_cleanup();
    return 0;
}
