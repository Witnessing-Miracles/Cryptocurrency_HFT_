#include <vector>
#include <map>
#include <cstdint>
#include <functional>
#include <utility>

using namespace std;

struct Level {
    double price{0}, qty{0};
};

struct DepthEvent {
    uint64_t U{0}, u{0}, pu{0};
    vector<Level> bids, asks;
};

struct OrderBook {
    map<double, double, greater<double>> bids;
    map<double, double> asks;

    uint64_t upateId{0};
    bool valid = false;
};

bool process(OrderBook &book, const DepthEvent& e, bool first) {
    if (e.u <= book.updateId)
        return true;

    if (first ? e.U > book.updateId : e.pu != book.updateId)
        return false;

    auto apply = [](auto& side, const auto& levels) {
        for (const auto& x : levels) {
            if (x.qty == 0) {
                side.erase(x.price);
            } else {
                side[x.price] = x.qty;
            }
        }
    }

    apply(book.bids, e.bids);
    apply(book.asks, e.asks);
    book.updateId = e.u;
    return true;
}

bool recover(OrderBook& book, const OrderBook& snapshot, const vector<DepthEvent>& buffer) {
    OrderBook next = snapshot;
    next.valid = false;
    const auto snapshotId = snapshot.updateId;
    bool first = true;

    for (const auto& e : buffer) {
        if (!process(next, e, first)) {
            book.valid = false;
            return false;
        }
        if (first && e.u > snapshotId)
            first = false;
    }

    if (first) {
        book.valid = false;
        return false;
    }

    next.valid = true;
    book = std::move(next);
    return true;
}


