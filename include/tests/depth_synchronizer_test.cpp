#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>

#include "md/depth_synchronizer.hpp"

using BinanceGW::MarketData::DepthEvent;
using BinanceGW::MarketData::DepthSnapshot;
using BinanceGW::MarketData::DepthSynchronizer;
using BinanceGW::MarketData::SyncState;
using BinanceGW::MarketData::UpdateId;

namespace {

auto make_event(UpdateId first_update_id,
                UpdateId final_update_id,
                UpdateId previous_final_update_id = 0) -> DepthEvent {
    DepthEvent event;
    event.first_update_id = first_update_id;
    event.final_update_id = final_update_id;
    event.previous_final_update_id = previous_final_update_id;
    return event;
}

auto make_snapshot(UpdateId last_update_id) -> DepthSnapshot {
    DepthSnapshot snapshot;
    snapshot.last_update_id = last_update_id;
    return snapshot;
}

// 增量先到时先缓存；收到快照后从桥接事件开始输出。
auto test_events_arrive_before_snapshot() -> void {
    DepthSynchronizer synchronizer;

    const auto waiting = synchronizer.on_event(make_event(95, 99));
    assert(waiting.state == SyncState::AwaitingSnapshot);
    assert(waiting.events_to_apply.empty());

    const auto waiting_again = synchronizer.on_event(make_event(98, 102));
    assert(waiting_again.state == SyncState::AwaitingSnapshot);
    assert(waiting_again.events_to_apply.empty());

    const auto synced = synchronizer.on_event(make_event(103, 105, 102));
    assert(synced.state == SyncState::AwaitingSnapshot);
    assert(synced.events_to_apply.empty());

    const auto result = synchronizer.on_snapshot(make_snapshot(100));
    assert(result.state == SyncState::Synchronized);
    assert(result.snapshot_to_apply.has_value());
    assert(result.snapshot_to_apply->last_update_id == 100);
    assert(result.events_to_apply.size() == 2);
    assert(result.events_to_apply[0].first_update_id == 98);
    assert(result.events_to_apply[0].final_update_id == 102);
    assert(result.events_to_apply[1].previous_final_update_id == 102);
    assert(result.events_to_apply[1].final_update_id == 105);
    assert(synchronizer.last_final_update_id().has_value());
    assert(*synchronizer.last_final_update_id() == 105);
}

// 快照先到时，丢弃快照已经包含的旧事件，并等待桥接事件。
auto test_snapshot_arrives_before_bridge() -> void {
    DepthSynchronizer synchronizer;

    const auto snapshot_result = synchronizer.on_snapshot(make_snapshot(200));
    assert(snapshot_result.state == SyncState::AwaitingBridge);
    assert(snapshot_result.events_to_apply.empty());

    const auto old_event = synchronizer.on_event(make_event(195, 199));
    assert(old_event.state == SyncState::AwaitingBridge);
    assert(old_event.events_to_apply.empty());

    const auto bridge_result = synchronizer.on_event(make_event(198, 202));
    assert(bridge_result.state == SyncState::Synchronized);
    assert(bridge_result.snapshot_to_apply.has_value());
    assert(bridge_result.snapshot_to_apply->last_update_id == 200);
    assert(bridge_result.events_to_apply.size() == 1);
    assert(bridge_result.events_to_apply[0].final_update_id == 202);
}

// 桥接之后 pu 必须等于前一事件的 u；否则整本簿需要重新同步。
auto test_gap_in_previous_update_id_requires_resync() -> void {
    DepthSynchronizer synchronizer;
    (void)synchronizer.on_event(make_event(98, 102));
    (void)synchronizer.on_event(make_event(103, 105, 102));

    const auto synced = synchronizer.on_snapshot(make_snapshot(100));
    assert(synced.state == SyncState::Synchronized);
    assert(synced.events_to_apply.size() == 2);

    const auto gap = synchronizer.on_event(make_event(106, 107, 104));
    assert(gap.state == SyncState::NeedResync);
    assert(gap.events_to_apply.empty());
    assert(!synchronizer.last_final_update_id().has_value());
}

// 正常同步后的连续事件立即交给调用方应用。
auto test_contiguous_live_event_is_forwarded() -> void {
    DepthSynchronizer synchronizer;
    (void)synchronizer.on_event(make_event(98, 102));
    const auto initial = synchronizer.on_snapshot(make_snapshot(100));
    assert(initial.state == SyncState::Synchronized);

    const auto next = synchronizer.on_event(make_event(103, 106, 102));
    assert(next.state == SyncState::Synchronized);
    assert(!next.snapshot_to_apply.has_value());
    assert(next.events_to_apply.size() == 1);
    assert(next.events_to_apply[0].final_update_id == 106);
    assert(*synchronizer.last_final_update_id() == 106);
}

// 快照之后才开始的首条增量没有覆盖快照序号，不能建立同步。
auto test_event_skips_past_snapshot_requires_resync() -> void {
    DepthSynchronizer synchronizer;
    (void)synchronizer.on_snapshot(make_snapshot(300));

    const auto result = synchronizer.on_event(make_event(301, 303));
    assert(result.state == SyncState::NeedResync);
    assert(result.events_to_apply.empty());
}

// 等待快照期间缓存达到上限后停止接收，避免无限占用内存。
auto test_buffer_limit_requires_resync() -> void {
    DepthSynchronizer synchronizer(1);

    const auto first = synchronizer.on_event(make_event(10, 11));
    assert(first.state == SyncState::AwaitingSnapshot);

    const auto overflow = synchronizer.on_event(make_event(12, 13, 11));
    assert(overflow.state == SyncState::NeedResync);
    assert(overflow.events_to_apply.empty());
}

// 显式重启同步流程会清理旧状态，允许重新接收快照和增量。
auto test_begin_resync_clears_state() -> void {
    DepthSynchronizer synchronizer;
    (void)synchronizer.on_event(make_event(98, 102));
    (void)synchronizer.on_snapshot(make_snapshot(100));
    assert(synchronizer.state() == SyncState::Synchronized);

    synchronizer.begin_resync();
    assert(synchronizer.state() == SyncState::AwaitingSnapshot);
    assert(!synchronizer.last_final_update_id().has_value());

    const auto buffered = synchronizer.on_event(make_event(198, 202));
    assert(buffered.state == SyncState::AwaitingSnapshot);
}

}  // namespace

int main() {
    test_events_arrive_before_snapshot();
    test_snapshot_arrives_before_bridge();
    test_gap_in_previous_update_id_requires_resync();
    test_contiguous_live_event_is_forwarded();
    test_event_skips_past_snapshot_requires_resync();
    test_buffer_limit_requires_resync();
    test_begin_resync_clears_state();

    std::cout << "All depth synchronizer tests passed.\n";
    return 0;
}
