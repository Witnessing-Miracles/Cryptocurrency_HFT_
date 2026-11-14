# DPDK 抓包示例：代码讲解

这份程序演示一个最基本的 DPDK 接收流程：初始化 EAL → 找到网卡 → 准备 mbuf 内存池 → 配置 RX 队列 → 收包并释放 → 退出时清理。

```
启动
  └─ 初始化 EAL
       └─ 检查 DPDK 网卡
            └─ 创建 mbuf 池
                 └─ 配置并启动网卡 RX 队列
                      └─ 循环收包、统计、释放
                           └─ Ctrl+C 后关闭网卡并清理
```

## 宏和全局变量

| 名称 | 含义 |
| --- | --- |
| `NUM_MBUFS` | 每个端口分配的 mbuf 数量基数。创建池时乘以可用端口数。 |
| `MBUF_CACHE_SIZE` | mbuf 池的每核缓存大小，用来减少频繁访问共享池的开销。 |
| `RX_RING_SIZE` | 希望设置的 RX 描述符数量；网卡驱动可能会调整为支持的值。 |
| `MAX_PKT_BURST` | 每次最多从 RX 队列批量取出的包数。 |
| `force_quit` | 收到 Ctrl+C（`SIGINT`）或终止信号（`SIGTERM`）后设为 1，让收包循环结束。 |
| `mbuf_pool` | DPDK mbuf 内存池指针。接收到的数据包由 mbuf 表示。 |
| `total_packets` | 已处理的数据包数量。 |
| `total_bytes` | 已处理的数据包总字节数。 |

`RTE_MBUF_DEFAULT_BUF_SIZE` 是 DPDK 提供的默认 mbuf 数据缓冲区大小；`RTE_ETH_FOREACH_DEV(port_id)` 是遍历所有可用 DPDK 网卡端口的宏。

## 函数说明

### `signal_handler(int signum)`

处理退出信号。它只设置 `force_quit` 标志，不在信号处理函数里做复杂工作。主循环看到该标志后结束。

### `process_packet(struct rte_mbuf *pkt)`

处理一个收到的 mbuf。当前示例只打印包长，并更新包数和总字节数；**没有解析 Ethernet/IP/TCP 头，也没有转发或发送数据包**。

### `port_init(uint16_t port_id)`

配置并启动一个端口。`port_id` 是 DPDK 的端口编号。

主要调用顺序：

1. `rte_eth_dev_configure(port_id, 1, 0, &port_conf)`：配置 1 个接收队列、0 个发送队列。

1. `rte_eth_dev_adjust_nb_rx_tx_desc(...)`：请求驱动调整 RX 描述符数量。

1. `rte_eth_rx_queue_setup(...)`：设置 RX 队列 0，并将 `mbuf_pool` 交给队列，用于存放收到的数据包。

1. `rte_eth_dev_start(port_id)`：启动网卡端口。

1. `rte_eth_promiscuous_enable(port_id)`：请求启用混杂模式。示例把它作为可选功能。

### `capture_loop(void)`

收包主循环。对每个端口调用：

```
rte_eth_rx_burst(port_id, 0, pkts, MAX_PKT_BURST)
```

参数依次是端口号、RX 队列号、用于接收 mbuf 指针的数组，以及本次最多接收的包数。函数返回本次实际收到的包数。程序逐包调用 `process_packet()`，随后用 `rte_pktmbuf_free()` 释放 mbuf；处理完后才可以归还这块缓冲区。

### `main(int argc, char **argv)`

程序入口，按顺序完成：

1. `rte_eal_init()`：初始化 DPDK EAL，并解析 EAL 命令行参数。

1. `rte_eth_dev_count_avail()`：检查可用 DPDK 网卡数量；为 0 时打印 `No Ethernet ports available` 并退出。

1. `rte_pktmbuf_pool_create()`：创建 mbuf 池。`NUM_MBUFS * nb_ports` 是池中 mbuf 总数；`0` 表示不额外分配每个 mbuf 的私有数据；`rte_socket_id()` 选择当前 NUMA socket。

1. 遍历端口并调用 `port_init()`。

1. 调用 `capture_loop()` 收包。

1. 收到退出信号后停止并关闭端口，打印统计，然后调用 `rte_eal_cleanup()` 清理 EAL。

## 运行时需要知道的事

- `-l 0-1` 是交给 EAL 的 lcore 参数；它本身不会让这份代码并行收包。当前代码没有启动 worker 函数，收包循环由运行 `main()` 的主 lcore 执行。

- `--no-huge` 请求不使用大页内存，常用于没有配置大页的学习环境；它不会让普通 Linux 网卡自动变成 DPDK 端口。

- 如果输出 `No Ethernet ports available`，表示 EAL 初始化成功，但没有发现可供 DPDK 使用的网卡。需要在支持的 Linux 环境中准备并绑定 DPDK 网卡后，才能实际收包。