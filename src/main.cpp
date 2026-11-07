#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include "md/binance_depth_types.hpp"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using json = nlohmann::json;
using BinanceGW::MarketData::DepthEvent;
using BinanceGW::MarketData::DepthSnapshot;
using BinanceGW::MarketData::PriceLevel;
using BinanceGW::MarketData::UpdateId;

namespace {

constexpr char kWsHost[] = "fstream.binance.com";
constexpr char kWsPort[] = "443";
constexpr char kWsTarget[] = "/public/stream?streams=btcusdt@depth";
constexpr char kRestHost[] = "fapi.binance.com";
constexpr char kRestTarget[] = "/fapi/v1/depth?symbol=BTCUSDT&limit=1000";
constexpr std::size_t kMaxBufferedEvents = 10000;

struct EventQueue {
    std::mutex mutex;
    std::condition_variable ready;
    std::vector<DepthEvent> events;
    bool connected{false};
    bool stopped{false};
    std::string error;
};

auto parse_levels(const json& levels) -> std::vector<PriceLevel> {
    std::vector<PriceLevel> result;
    result.reserve(levels.size());
    for (const auto& item : levels) {
        result.push_back(PriceLevel{item.at(0).get<std::string>(),
                                    item.at(1).get<std::string>()});
    }
    return result;
}

auto parse_event(const std::string& text) -> DepthEvent {
    const auto root = json::parse(text);
    // /public/stream 返回 combined-stream 外壳，实际事件在 data 字段中。
    const auto& data = root.at("data");
    DepthEvent event;
    event.first_update_id = data.at("U").get<UpdateId>();
    event.final_update_id = data.at("u").get<UpdateId>();
    event.previous_final_update_id = data.at("pu").get<UpdateId>();
    event.bids = parse_levels(data.at("b"));
    event.asks = parse_levels(data.at("a"));
    return event;
}

auto fetch_snapshot() -> DepthSnapshot {
    asio::io_context io;
    asio::ssl::context ssl_context(asio::ssl::context::tls_client);
    ssl_context.set_default_verify_paths();
    ssl_context.set_verify_mode(asio::ssl::verify_peer);

    tcp::resolver resolver(io);
    beast::ssl_stream<beast::tcp_stream> stream(io, ssl_context);
    if (!SSL_set_tlsext_host_name(stream.native_handle(), kRestHost)) {
        throw std::runtime_error("无法设置 REST TLS SNI");
    }
    stream.set_verify_callback(asio::ssl::host_name_verification(kRestHost));
    const auto endpoints = resolver.resolve(kRestHost, "443");
    beast::get_lowest_layer(stream).connect(endpoints);
    stream.handshake(asio::ssl::stream_base::client);

    http::request<http::empty_body> request{http::verb::get, kRestTarget, 11};
    request.set(http::field::host, kRestHost);
    request.set(http::field::user_agent, "BinanceGatewayStarter/1.0");
    request.set(http::field::accept, "application/json");
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    beast::error_code ignored;
    stream.shutdown(ignored);

    if (response.result() != http::status::ok) {
        throw std::runtime_error("REST 快照请求失败，HTTP " +
                                 std::to_string(response.result_int()) + ": " + response.body());
    }

    const auto body = json::parse(response.body());
    DepthSnapshot snapshot;
    snapshot.last_update_id = body.at("lastUpdateId").get<UpdateId>();
    snapshot.bids = parse_levels(body.at("bids"));
    snapshot.asks = parse_levels(body.at("asks"));
    return snapshot;
}

// 单独线程只负责收 WSS 并入队；同步逻辑留在主线程，避免共享状态机。
auto receive_websocket(EventQueue& queue) -> void {
    try {
        asio::io_context io;
        asio::ssl::context ssl_context(asio::ssl::context::tls_client);
        ssl_context.set_default_verify_paths();
        ssl_context.set_verify_mode(asio::ssl::verify_peer);

        tcp::resolver resolver(io);
        websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(io, ssl_context);
        if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), kWsHost)) {
            throw std::runtime_error("无法设置 WebSocket TLS SNI");
        }
        ws.next_layer().set_verify_callback(asio::ssl::host_name_verification(kWsHost));
        const auto endpoints = resolver.resolve(kWsHost, kWsPort);
        beast::get_lowest_layer(ws).connect(endpoints);
        ws.next_layer().handshake(asio::ssl::stream_base::client);
        ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws.handshake(kWsHost, kWsTarget);

        {
            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.connected = true;
        }
        queue.ready.notify_all();
        std::cout << "WSS 已连接：" << kWsTarget << '\n';

        for (;;) {
            beast::flat_buffer buffer;
            ws.read(buffer);
            auto event = parse_event(beast::buffers_to_string(buffer.data()));

            std::lock_guard<std::mutex> lock(queue.mutex);
            if (queue.events.size() >= kMaxBufferedEvents) {
                queue.error = "WebSocket 事件缓存已满";
                queue.stopped = true;
                queue.ready.notify_all();
                return;
            }
            queue.events.push_back(std::move(event));
            queue.ready.notify_all();
        }
    } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lock(queue.mutex);
        queue.error = error.what();
        queue.stopped = true;
        queue.ready.notify_all();
    }
}

// 0 表示还不够数据；1 表示成功衔接；-1 表示快照与缓存之间有缺口。
auto try_bridge(const DepthSnapshot& snapshot,
                std::vector<DepthEvent>& pending,
                std::optional<UpdateId>& last_update_id) -> int {
    std::size_t first = 0;
    while (first < pending.size() && pending[first].final_update_id < snapshot.last_update_id) {
        ++first;
    }
    pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(first));

    if (pending.empty()) {
        return 0;
    }

    const auto& bridge = pending.front();
    if (bridge.first_update_id > snapshot.last_update_id) {
        return -1;
    }
    if (bridge.final_update_id < snapshot.last_update_id) {
        return 0;
    }

    for (std::size_t i = 1; i < pending.size(); ++i) {
        if (pending[i].previous_final_update_id != pending[i - 1].final_update_id) {
            return -1;
        }
    }

    last_update_id = pending.back().final_update_id;
    std::cout << "行情同步成功：snapshot=" << snapshot.last_update_id
              << ", 首个桥接事件 [U=" << bridge.first_update_id
              << ", u=" << bridge.final_update_id << "]"
              << ", 当前 u=" << *last_update_id << '\n';
    // 这里只演示快照/增量恢复；订单簿应用留到下一步。
    pending.clear();
    return 1;
}

auto main_loop(EventQueue& queue) -> int {
    std::vector<DepthEvent> pending;
    std::optional<DepthSnapshot> snapshot;
    std::optional<UpdateId> last_update_id;
    bool synchronized = false;

    // WSS 已成功订阅且接收线程开始缓存后，再请求 REST 快照。
    snapshot = fetch_snapshot();
    std::cout << "收到 REST 快照：lastUpdateId=" << snapshot->last_update_id
              << ", bids=" << snapshot->bids.size()
              << ", asks=" << snapshot->asks.size() << '\n';

    for (;;) {
        std::vector<DepthEvent> incoming;
        {
            std::unique_lock<std::mutex> lock(queue.mutex);
            queue.ready.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return !queue.events.empty() || queue.stopped;
            });
            incoming.swap(queue.events);
            if (queue.stopped && incoming.empty()) {
                throw std::runtime_error(queue.error.empty() ? "WebSocket 已停止" : queue.error);
            }
        }

        for (auto& event : incoming) {
            if (synchronized) {
                if (event.previous_final_update_id == *last_update_id) {
                    last_update_id = event.final_update_id;
                    std::cout << "增量连续：pu=" << event.previous_final_update_id
                              << ", u=" << event.final_update_id << '\n';
                    continue;
                }

                std::cerr << "检测到序号缺口：期望 pu=" << *last_update_id
                          << ", 实际 pu=" << event.previous_final_update_id
                          << "，重新获取快照\n";
                synchronized = false;
                last_update_id.reset();
                pending.clear();
                pending.push_back(std::move(event));
                snapshot = fetch_snapshot();
                std::cout << "重新收到快照：lastUpdateId=" << snapshot->last_update_id << '\n';
            } else {
                if (pending.size() >= kMaxBufferedEvents) {
                    pending.clear();
                    snapshot = fetch_snapshot();
                    std::cout << "缓存达到上限，清空缓存并重新获取快照：lastUpdateId="
                              << snapshot->last_update_id << '\n';
                }
                pending.push_back(std::move(event));
            }

            if (!synchronized && snapshot) {
                const int result = try_bridge(*snapshot, pending, last_update_id);
                if (result == 1) {
                    synchronized = true;
                    snapshot.reset();
                } else if (result == -1) {
                    std::cerr << "当前缓存无法衔接快照，重新获取快照\n";
                    pending.clear();
                    snapshot = fetch_snapshot();
                }
            }
        }
    }
}

}  // namespace

int main() {
    try {
        // detached 接收线程通过 shared_ptr 持有队列；主线程报错退出时不会访问已销毁对象。
        auto queue = std::make_shared<EventQueue>();
        std::thread([queue] { receive_websocket(*queue); }).detach();
        {
            std::unique_lock<std::mutex> lock(queue->mutex);
            queue->ready.wait(lock, [&] { return queue->connected || queue->stopped; });
            if (!queue->connected) {
                throw std::runtime_error(queue->error.empty() ? "WebSocket 连接失败" : queue->error);
            }
        }

        return main_loop(*queue);
    } catch (const std::exception& error) {
        std::cerr << "错误：" << error.what() << '\n';
        return 1;
    }
}
