#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BinanceGW::MarketData {

using UpdateId = std::uint64_t;

// 第一版保留交易所返回的十进制文本，避免未读取交易规则就猜测价格/数量精度。
struct PriceLevel {
    std::string price;
    std::string quantity;
};

// Binance USD-M Futures 的一条深度增量事件。
struct DepthEvent {
    UpdateId first_update_id{0};           // U：本事件覆盖的起始更新序号
    UpdateId final_update_id{0};           // u：本事件覆盖的结束更新序号
    UpdateId previous_final_update_id{0};  // pu：上一条事件的结束序号
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

// REST 返回的有限深度快照。
struct DepthSnapshot {
    UpdateId last_update_id{0};  // 快照包含到的更新序号
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

}  // namespace BinanceGW::MarketData
