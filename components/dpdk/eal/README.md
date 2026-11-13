# DPDK EAL 运行结果解析

## 1. 编译与运行

首次使用需安装 DPDK 开发包（提供 `libdpdk.pc` 与头文件）：

```bash
sudo apt update
sudo apt install -y dpdk dpdk-dev
pkg-config --modversion libdpdk      # 验证，应输出类似于 23.11.7
```

编译（在 `components/dpdk/eal` 目录，工程通过 pkg-config 查找 DPDK）：

```bash
cd components/dpdk/eal
cmake -S . -B build
cmake --build build
```

运行（WSL2 无大页内存，需加 `--no-huge`）：

```bash
sudo ./bin/eal_hello -l 1-5 --no-huge
```

输出：

```
EAL: Detected CPU lcores: 8
EAL: Detected NUMA nodes: 1
EAL: Static memory layout is selected, amount of reserved memory can be adjusted with -m or --socket-mem
EAL: Detected shared linkage of DPDK
EAL: Multi-process socket /var/run/dpdk/rte/mp_socket
EAL: Selected IOVA mode 'VA'
TELEMETRY: No legacy callbacks, legacy socket not created
EAL: lib.eal log level changed from info to debug
worker lcore id: 2
hello from core 2
worker lcore id: 3
hello from core 3
worker lcore id: 4
hello from core 4
worker lcore id: 5
hello from core 5
hello from core 1
```

## 2. 输出逐行解读

| 输出 | 含义 |
| --- | --- |
| `Detected CPU lcores: 8` | EAL 探测到 WSL2 虚拟机有 8 个逻辑核 |
| `Detected NUMA nodes: 1` | 只有 1 个 NUMA 节点，内存访问无跨节点差异 |
| `Static memory layout ...` | 使用静态内存布局，可用 `-m` / `--socket-mem` 调整预留大小 |
| `Selected IOVA mode 'VA'` | DMA 地址模式为虚拟地址 |
| `lib.eal log level changed from info to debug` | 代码中 `rte_log_set_level(RTE_LOGTYPE_EAL, RTE_LOG_DEBUG)` 生效 |
| `worker lcore id: 2/3/4/5` | **主线程**在循环里打印，随后用 `rte_eal_remote_launch` 把任务派给该核 |
| `hello from core 2/3/4/5` | 各 worker 线程自己打印 |
| `hello from core 1` | **主核**直接调用任务函数打印 |

## 3. 关键结论

### 3.1 输出不是"按参数造假"，而是真实运行在指定 CPU 上

`-l 1-5` 不是让程序打印 1~~5，而是限制 EAL **只能使用 CPU 1~~5**。`rte_eal_init()` 会：

1. 探测真实 CPU 数量（本机 8 个逻辑核）；

1. 为 `-l` 列表里的每个核创建一个 pthread，并绑定（`pthread_setaffinity`）到对应 CPU；

1. 每个线程中 `rte_lcore_id()` 返回自己绑定的核编号（默认 1:1 映射，lcore id 即 CPU 编号）。

因此程序确实在那几个物理核上运行，每个线程打印自己的编号，输出自然与参数一致。若传入不存在的核（如本机 6 核时用 `-l 0-9`），EAL 会直接报错拒绝启动：`invalid core list`。

### 3.2 main lcore 是列表中编号最小的核

`-l 1-5` 排除了核 0，EAL 取列表中最小的 1 作为 **main lcore**（运行 `main()`），其余 2~5 为 worker。

- `-l 0-3` → main = 0，worker = 1/2/3

- `-l 1-5` → main = **1**，worker = 2/3/4/5

把 0 号核排除，相当于把它留给系统或其他进程，是"隔离核"思路的雏形。

### 3.3 打印顺序不固定

`worker lcore id:` 由主线程打印，`hello from core X` 由各核并行打印，先后顺序取决于调度。本次 core 1 最后打印，说明 worker 先执行完，属正常现象。

## 4. IOVA mode 是什么

**IOVA = I/O Virtual Address，设备做 DMA 时使用的地址。**

CPU 访问内存使用虚拟地址（VA），但网卡等设备不认识 CPU 页表，DMA 时使用 IOVA。中间由 **IOMMU**（Intel 称 VT-d）把 IOVA 翻译为物理地址（PA）；没有 IOMMU 时，IOVA 必须直接等于物理地址。

| 模式 | 前提 | 设备看到的地址 | 特点 |
| --- | --- | --- | --- |
| **VA**（虚拟地址） | 有 IOMMU，网卡绑定 `vfio-pci` | 进程虚拟地址 | 无需为每个 buffer 维护虚拟→物理映射，`buf_iova` 等于虚拟地址；64 位空间、多进程友好，现代部署首选 |
| **PA**（物理地址） | 无 IOMMU（UIO / vfio-noiommu） | 物理地址 | 需记录每块大页的物理地址，设备用物理地址 DMA；依赖物理地址可用性 |

EAL 根据 IOMMU 是否启用、设备驱动类型自动选择，也可用 `--iova-mode=va|pa` 强制指定。

与 Hello World 的关系：该程序不涉及网卡，IOVA 只是 EAL 的初始化信息，不影响输出。后续收包程序中，mbuf 需要交给网卡 DMA，`rte_mbuf` 的 `buf_addr`（CPU 使用的虚拟地址）与 `buf_iova`（网卡使用的 IOVA）正是为该模式设计的：VA 模式下两者相等，PA 模式下不同。

## 5. WSL2 注意事项

- WSL2 默认未预留大页内存，运行需加 `--no-huge`，否则报 `Cannot get hugepage information`。

- WSL2 无法将真实网卡直通给 DPDK，需要网卡的程序（收包、流管理等）无法在 WSL2 上完整运行，需使用物理机或支持 SR-IOV 的云主机。