#include <cassert>
#include <iostream>
#include <optional>
#include <vector>

#include "md/binance_depth_sync.hpp"

using BinanceGW::MarketData::DepthEvent;
using BinanceGW::MarketData::UpdateId;
using BinanceGW::MarketData::find_snapshot_bridge;

namespace {

// 构造一条只包含序号信息的测试事件；本测试不检查 bid/ask 档位。
auto make_event(UpdateId first_update_id,
                UpdateId final_update_id,
                UpdateId previous_final_update_id = 0) -> DepthEvent {
    DepthEvent event;
    event.first_update_id = first_update_id;
    event.final_update_id = final_update_id;
    event.previous_final_update_id = previous_final_update_id;
    return event;
}

// 缓存中的所有事件都早于快照，应找不到桥接事件。
auto test_all_events_are_older_than_snapshot() -> void {
    const std::vector<DepthEvent> events{
        make_event(95, 98),
        make_event(98, 99),
    };

    const auto bridge = find_snapshot_bridge(events, 100);
    assert(!bridge.has_value());
}

// 跳过早于快照的事件，找到覆盖快照序号的事件，并返回其索引。
auto test_finds_bridge_after_older_events() -> void {
    const std::vector<DepthEvent> events{
        make_event(95, 99),
        make_event(98, 103),
    };

    const auto bridge = find_snapshot_bridge(events, 100);
    assert(bridge.has_value());
    assert(*bridge == 1);
}

// 第一条不早于快照的事件已经从快照之后开始，代表中间有缺口。
auto test_event_starts_after_snapshot() -> void {
    const std::vector<DepthEvent> events{
        make_event(95, 99),
        make_event(101, 103),
    };

    const auto bridge = find_snapshot_bridge(events, 100);
    assert(!bridge.has_value());
}

// 事件的最后序号恰好等于快照序号时，边界仍然应该匹配。
auto test_event_ends_at_snapshot_id() -> void {
    const std::vector<DepthEvent> events{
        make_event(98, 100),
    };

    const auto bridge = find_snapshot_bridge(events, 100);
    assert(bridge.has_value());
    assert(*bridge == 0);
}

// 空缓存没有可用于衔接快照的事件。
auto test_empty_event_list() -> void {
    const std::vector<DepthEvent> events;

    const auto bridge = find_snapshot_bridge(events, 100);
    assert(!bridge.has_value());
}

}  // namespace

int main() {
    test_all_events_are_older_than_snapshot();
    test_finds_bridge_after_older_events();
    test_event_starts_after_snapshot();
    test_event_ends_at_snapshot_id();
    test_empty_event_list();

    std::cout << "All Binance depth sync tests passed.\n";
    return 0;
}
