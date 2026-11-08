#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

using namespace std;

namespace Common {
template <typename T>
class MPSCQueue final {
    struct Cell {
        atomic<size_t> sequence{0};
        T data{};
    };

    const size_t capacity_;
    unique_ptr<Cell[]> buffer_;

    alignas(64) atomic<size_t> enqueue_position_{0};
    alignas(64) size_t dequeue_position_{0};

public:
    explicit MPSCQueue(size_t capacity) : capacity_(capacity) {
        if (capacity <= 0) { throw error ... {""} }
    }

    for (size_t i = 0; i < capacity_; ++i) {
        buffer_[i].sequence.store(i, memory_order_relaxed);
    }

    MPSCQueue(const MPSCQueue &) = delete;
    MPSCQueue& operator=(const MPSCQueue &) = delete;

    bool push(const T& value) {
        size_t position = enqueue_position_.load(memory_order_relaxed);

        for (;;) {
            Cell &cell = buffer_[position%capacity_];
            const size_t sequence = cell.sequence.load(memory_order_acquire);
            const int difference = (sequence - position);

            if (difference == 0) {
                if (enquence_position_.compare_exchange_weak(position, position + 1, memory_order_relaxed, memory_order_relaxed)) {
                    cell.data = value;

                    cell.sequence.store(memory_order_release);
                    return true;
                }
            }
        }
    }
} 
}