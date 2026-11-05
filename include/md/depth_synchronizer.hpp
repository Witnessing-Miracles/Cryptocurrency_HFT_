#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "md/binance_depth_sync.hpp"
#include "md/binance_depth_types.hpp"

namespace BinanceGW::MarketData {

enum class SyncState {
    AwaitingSnapshot,  // 尚未收到 REST 快照。
    AwaitingBridge,    // 已有快照，等待能与快照序号衔接的增量事件。
    Synchronized,      // 快照和增量已经衔接，后续事件可连续应用。
    NeedResync         // 发现缺口或缓存溢出，当前同步结果不可再信任。
};

struct SyncResult {
    SyncState state{SyncState::AwaitingSnapshot};

    // 同步建立时返回快照。调用方必须先用它初始化订单簿，再应用下面的增量。
    // 快照可能在收到 REST 响应时仍缺桥接事件，因此也可能随 on_event 的结果返回。
    std::optional<DepthSnapshot> snapshot_to_apply;

    // 只有这些事件可以安全地按顺序应用到本地订单簿。
    // 未同步、缺口或缓存溢出时此列表为空。
    std::vector<DepthEvent> events_to_apply;
};

/// 管理单个交易品种的快照/增量衔接和序号连续性。
/// 所有方法应由同一个线程依序调用；网络线程并发调用时需在外层串行化。
class DepthSynchronizer {
public:
    /// max_buffered_events 限制等待快照期间的事件缓存，防止内存无限增长。
    explicit DepthSynchronizer(std::size_t max_buffered_events = 10000)
        : max_buffered_events_(max_buffered_events) {}

    /// 开始一次新的同步流程，丢弃之前缓存和序号状态。
    auto begin_resync() -> void {
        buffered_events_.clear();
        snapshot_.reset();
        last_final_update_id_.reset();
        state_ = SyncState::AwaitingSnapshot;
    }

    /// 收到一条 WebSocket 深度事件。
    /// 同步前先缓存；同步后检查 pu，并在连续时把事件交给订单簿。
    [[nodiscard]]
    auto on_event(DepthEvent event) -> SyncResult {
        if (state_ == SyncState::NeedResync) {
            return result();
        }

        if (state_ == SyncState::Synchronized) {
            if (!is_valid_event(event) ||
                event.previous_final_update_id != *last_final_update_id_ ||
                event.final_update_id <= *last_final_update_id_) {
                invalidate();
                return result();
            }

            last_final_update_id_ = event.final_update_id;
            SyncResult output{state_, std::nullopt, {}};
            output.events_to_apply.push_back(std::move(event));
            return output;
        }

        if (buffered_events_.size() >= max_buffered_events_) {
            invalidate();
            return result();
        }

        if (!is_valid_event(event)) {
            invalidate();
            return result();
        }

        buffered_events_.push_back(std::move(event));
        return try_synchronize();
    }

    /// 收到 REST 深度快照。
    /// 返回的 events_to_apply 从桥接事件开始，调用方应先装载快照，再依序应用这些事件。
    [[nodiscard]]
    auto on_snapshot(DepthSnapshot snapshot) -> SyncResult {
        if (state_ == SyncState::NeedResync) {
            return result();
        }

        snapshot_ = std::move(snapshot);
        state_ = SyncState::AwaitingBridge;
        return try_synchronize();
    }

    [[nodiscard]]
    auto state() const noexcept -> SyncState {
        return state_;
    }

    [[nodiscard]]
    auto last_final_update_id() const noexcept -> std::optional<UpdateId> {
        return last_final_update_id_;
    }

private:
    [[nodiscard]]
    static auto is_valid_event(const DepthEvent& event) noexcept -> bool {
        return event.first_update_id <= event.final_update_id &&
               event.previous_final_update_id < event.final_update_id;
    }

    auto invalidate() -> void {
        state_ = SyncState::NeedResync;
        buffered_events_.clear();
        snapshot_.reset();
        last_final_update_id_.reset();
    }

    [[nodiscard]]
    auto result() const -> SyncResult {
        return SyncResult{state_, std::nullopt, {}};
    }

    auto try_synchronize() -> SyncResult {
        if (!snapshot_) {
            state_ = SyncState::AwaitingSnapshot;
            return result();
        }

        // 删掉快照已经包含的旧事件，避免旧数据占满有界缓存。
        const auto snapshot_id = snapshot_->last_update_id;
        std::size_t first_candidate = 0;
        while (first_candidate < buffered_events_.size() &&
               buffered_events_[first_candidate].final_update_id < snapshot_id) {
            ++first_candidate;
        }
        if (first_candidate != 0) {
            buffered_events_.erase(buffered_events_.begin(),
                                   buffered_events_.begin() + static_cast<std::ptrdiff_t>(first_candidate));
        }

        if (buffered_events_.empty()) {
            state_ = SyncState::AwaitingBridge;
            return result();
        }

        const auto bridge_index = find_snapshot_bridge(buffered_events_, snapshot_id);
        if (!bridge_index.has_value()) {
            // 第一条未被快照覆盖的事件若从快照之后才开始，说明中间有缺口。
            // find_snapshot_bridge 在旧事件之后遇到这种情况会返回空。
            invalidate();
            return result();
        }

        // 桥接事件由 REST 快照覆盖到其 U..u 区间，因此不检查它的 pu；
        // 从下一条事件开始，pu 必须等于上一条事件的 u。
        for (std::size_t i = *bridge_index + 1; i < buffered_events_.size(); ++i) {
            if (buffered_events_[i].previous_final_update_id !=
                buffered_events_[i - 1].final_update_id) {
                invalidate();
                return result();
            }
        }

        SyncResult output{SyncState::Synchronized, std::nullopt, {}};
        output.events_to_apply.reserve(buffered_events_.size() - *bridge_index);
        for (std::size_t i = *bridge_index; i < buffered_events_.size(); ++i) {
            output.events_to_apply.push_back(std::move(buffered_events_[i]));
        }

        last_final_update_id_ = output.events_to_apply.back().final_update_id;
        buffered_events_.clear();
        output.snapshot_to_apply = std::move(snapshot_);
        snapshot_.reset();
        state_ = SyncState::Synchronized;
        output.state = state_;
        return output;
    }

    const std::size_t max_buffered_events_;
    SyncState state_{SyncState::AwaitingSnapshot};
    std::optional<DepthSnapshot> snapshot_;
    std::vector<DepthEvent> buffered_events_;
    std::optional<UpdateId> last_final_update_id_;
};

}  // namespace BinanceGW::MarketData
