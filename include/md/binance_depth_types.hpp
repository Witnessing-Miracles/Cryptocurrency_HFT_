#pragma once

#include <cstdint>
#include <vector>

namespace BinanceGW::MarketData {

using UpdateId = std::uint64_t;

/// 一个交易品种的交易规则
/// 价格和数量都先按 decimal_scale 放大成整数
/// 例如 decimal_scale = 1000 时，67234.5会表示为67234500
struct InstrumentRules {
    std::int64_t decimal_scale{0};

    /// 以下栏位都使用同一个 decimal_scale 表示
    std::int64_t min_price_units{0};
    std::int64_t tick_size_units{0};
    std::int64_t min_qty_units{0};
    std::int64_t step_size_units{0};
};

/// 一个价格档位及其聚合数量
/// price_ticks 是相对 minPrice 的 tick 索引
/// qty_units 是按decimal_scale放大后的绝对数量
struct PriceLevel {
    std::int64_t price_ticks{0};
    std::int64_t qty_units{0};
};

/// Binance Futures 的一笔深度增量事件
/// 同一事件可以同时更新多个 bid 和 ask 价格档位
struct DepthEvent {
    UpdateId first_update_id{0};            // U: 事件涵盖的第一个更新序号
    UpdateId final_update_id{0};            // u: 事件涵盖的最后一个更新序号
    UpdateId previous_final_update_id{0};   // pu: 上一笔事件的最后更新序号

    /// 增量中的数量是该价格档更新后的绝对数量，不是加减量
    /// 数量为 0 表示删除该价格档
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

/// REST 回传的深度快照
struct DepthSnapshot {
    UpdateId last_update_id{0};         // 快照包含到的最后更新的序号
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

}