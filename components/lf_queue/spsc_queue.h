#pragma once

#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Common {
// 单生产者/消费者(SPSC)有界环形队列
// 使用约束：只能有一个线程调用 tryPush，另一个线程调用 tryPop
// T 需要默认构造，并支持赋值；队列创建后不再改变容量
template <typename T>
class SPSCQueue final {
    alignas(64) std::atomic<std::size_t> write_index_{0};
    alignas(64) std::atomic<std::size_t> read_index_{0};

    std::vector<T> buffer_;

    static std::size_t checkedStorageSize(std::size_t capacity) {
        if (capacity <= 0) {
            throw std::invalid_argument("SPSCQueue capacity must be greater than zero.");
        }
        if (capacity == std::numeric_limits<std::size_t>::max()) {
            throw std::length_error("SPSCQueue capacity is too large.");
        }

        // 多留一个槽位，用于区分队列满和队列空，因此底层大小是 capacity + 1
        return capacity + 1;
    }

    std::size_t next(std::size_t index) const noexcept {
        ++index;
        return index == buffer_.size() ? 0 : index;
    }
public:
    // capacity 是可存放的实际元素数量，不是底层数组大小
    explicit SPSCQueue(std::size_t capacity) : buffer_(checkedStorageSize(capacity)) {}

    SPSCQueue() = delete;
    SPSCQueue(const SPSCQueue &) = delete;
    SPSCQueue(SPSCQueue &&) = delete;
    SPSCQueue &operator=(const SPSCQueue &) = delete;
    SPSCQueue &operator=(SPSCQueue &&) = delete;

    // 生产者调用，队列满返回 false，不覆盖尚未消费的数据
    bool tryPush(const T & value) {
        std::size_t write = write_index_.load(std::memory_order_relaxed);
        std::size_t next_write = next(write);

        // 读取消费者发布的 read_index，确认该槽位已被消费，可以复用
        if (next_write == read_index_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[write] = value;

        // release 保证上面的元素写入先于 write_index 对消费者可见
        write_index_.store(next_write, std::memory_order_relaxed);
        return true;
    }

    // 消费者调用。队列空时返回 false
    bool tryPop(T &out) {
        const std::size_t read = read_index_.load(std::memory_order_relaxed);

        // acquire 与生产者的 release 配对：看到新 write_index 后
        // 也能看到生产者此前写入的元素内容
        if (read == write_index_.load(std::memory_order_acquire)) {
            return false;
        }

        out = buffer_[read];

        // release 告诉生产者：当前槽位已经读完，可以在未来复用
        read_index_.store(next(read), std::memory_order_release);
        return true;
    }

    std::size_t capacity() const noexcept {
        return buffer_.size() - 1;
    }
};

}