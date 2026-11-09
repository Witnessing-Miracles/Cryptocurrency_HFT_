#pragma once

#include "../../common/types.h"
#include "../../common/lf_queue.h"

using namespace Common;

namespace Exchange {
    /// 在市场更新信息中表示类型以及操作
    enum class MarketUpdateType : uint8_t {
        INVALID = 0,
        CLEAR = 1,
        ADD = 2,
        MODIFY = 3,
        CANCEL = 4,
        TRADE = 5,
        SNAPSHOT_START = 6,
        SNAPSHOT_END = 7
    };

    inline std::string marketUpdateTypeToString(MarketUpdateType type) {
        switch (type) {
      case MarketUpdateType::CLEAR:
        return "CLEAR";
      case MarketUpdateType::ADD:
        return "ADD";
      case MarketUpdateType::MODIFY:
        return "MODIFY";
      case MarketUpdateType::CANCEL:
        return "CANCEL";
      case MarketUpdateType::TRADE:
        return "TRADE";
      case MarketUpdateType::SNAPSHOT_START:
        return "SNAPSHOT_START";
      case MarketUpdateType::SNAPSHOT_END:
        return "SNAPSHOT_END";
      case MarketUpdateType::INVALID:
        return "INVALID";
    }
    return "UNKNOWN";
    }

#pragma pack(push, 1) // 它们用于临时取消编译器的内存对齐机制，强制让结构体内的变量紧紧挨在一起（1字节对齐），结束后再恢复默认设置。

    /// Market update structure used internally by the matching engine.
    struct MEMarketUpdate {
        MarketUpdateType type_ = MarketUpdateType::INVALID;

        OrderId order_id_ = OrderId_Invalid;
        TickerId ticker_id_ = TickerId_INVALID
        Side side_ = Side::INVALID;
        Price price_ = Price_INVALID;
        Qty qty = Qty_INVALID;
        Priority priority_ = Priority_INVALID;

        auto toString() const {
      std::stringstream ss;
      ss << "MEMarketUpdate"
         << " ["
         << " type:" << marketUpdateTypeToString(type_)
         << " ticker:" << tickerIdToString(ticker_id_)
         << " oid:" << orderIdToString(order_id_)
         << " side:" << sideToString(side_)
         << " qty:" << qtyToString(qty_)
         << " price:" << priceToString(price_)
         << " priority:" << priorityToString(priority_)
         << "]";
      return ss.str();
    }
    };

    /// Market update structure published over the network by the market data publisher
    struct MDPMarketUpdate {
        size_t seq_num = 0;
        MEMarketUpdate me_market_update_;

        auto toString() const {
            std::stringstream ss;
            ss << "MDPMarketUpdate"
                << " ["
                << " seq:" << seq_num_
                << " " << me_market_update_.toString()
                << "]";
            return ss.str();
        }
    };

#pragma pack(pop)   // 在某些场景下，效率不是第一位的，字节的精确排布才是第一位的。 #pragma pack(push, 2) 按2字节对齐

    /// Lock free queues of matching engine market update messages and market data publisher
   // market updates messages respectively
   typedef Common::LFQueue<Exchange::MEMarketUpdate> MEMarketUpdateLFQueue;
   typedef Common::LFQueue<Exchange::MDPMarketUpdate> MDPMarketUpdateLFQueue;
}
