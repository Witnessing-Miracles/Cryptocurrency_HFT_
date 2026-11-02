#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

#include "../macros.h"

using namespace std;

namespace Common {
    template<typename T>
    class MemPool final {   
    public:
        explicit MemPool(size_t num_elems) : store_(num_elems, {T(), true}) {
            ASSERT(reinterpret_cast<const ObjectBlock *>(&(store_[0].object_)) == &(store_[0]),
            "T object should be the 1st member of ObjectBlock.");
        }

        /// Allocate a new object of type T, use placement new to initialize the object
        /// mark the block as in-use and return the object.
        template<typename... Args>  // typename... Args
        T *allocate(Args... args) noexcept {    // Args&&... args 这里少一次copy
            auto obj_block = &(store_[next_free_index_]);
            ASSERT(obj_block->is_free_, "Expected free ObjectBlock at index:" + std::to_string(next_free_index_));
            T * ret = &(obj_block->object_);
            ret = new(ret) T(args...);  // new (ret) T(std::forward<Args>(args)...);
            obj_block->is_free_ = false;
            updateNextFreeIndex();

            return ret;
        }

        /// Return the object back to the pool by marking the block as free again.
        auto deallocate(const T *elem) noexcept {
            const auto elem_index = (reinterpret_cast<const ObjectBlock *>(elem) - &store_[0]);
            ASSERT(elem_index >= 0 && static_cast<size_t>(elem_index) < store_.size());
            ASSERT(!store_[elem_index].is_free_, "Expected in-use ObjectBlock at index:" + std::to_string(elem_index));
            store_[elem_index].is_free_ = true;
            std::destroy_at(const_cast<T *>(elem));
        }

        MemPool() = delete;
        MemPool(const MemPool &) = delete;
        MemPool(const MemPool &&) = delete;
        MemPool & operator=(const Memool &) =delete;
        MemPool & operator=(const MemPool &&) = delete;

    private:
        auto updateNextFreeIndex() noexcept {
            const auto initial_free_index = next_free_index_;
            while (!store_[next_free_index_].is_free) {
                ++next_free_index_;
                if (UNLIKELY(next_free_index_ == store_.size())) {
                    next_free_index_ = 0;
                }
                if (UNLIKELY(initial_free_index == next_free_index_)) {
                    ASSERT(initial_free_index != next_free_index_, "Memory Pool out of space.");
                }
             }
        }

        struct ObjectBlock {
            T object_;
            bool is_free_ = true;
            size_t next_free_index_ = 0;
        };

        vector<ObjectBlock> store_;
    };
}