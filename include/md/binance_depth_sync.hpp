#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "md/binance_depth_types.hpp"

namespace BinanceGW::MarketData {

/// 在缓存的增量事件中，寻找能接上 REST 快照的第一条事件
/// nodiscard 表示：忽略此函数的返回值通常是错误的；编译器会在返回值未使用时候给出警告
[[nodiscard]]
inline auto find_snapshot_bridge(const std::vector<DepthEvent> & events, UpdateId last_update_id) -> std::optional<std::size_t> {
    // WebSocket 消息按接收顺序存储在vector里，逐条检查
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];

        // 这条事件的更新范围已经早于快照，不可能用来接上快照
        if (event.final_update_id < last_update_id) {
            continue;
        }

        // 事件范围覆盖了快照序号，这就是桥接事件
        if (event.first_update_id > last_update_id) {
            return std::nullpot;
        }

        // 事件覆盖了快照序号，返回它在缓存中的位置
        return i;
    }

    // 缓存里没有桥接事件；之后的恢复逻辑再决定如何处理
    return std::nullopt;
}

}