#pragma once

#include <functional>

#include "snapshot_synthesizer.h"

namespace Exchange {
    class MarketDataPublisher {
    public:
        MarketDataPublisher(MEMarketUpdateLFQueue *market_updates, const std::string &iface,
                            const std::string &snapshot_ip, int snapshot_port,
                            const std::string &incremental_ip, int incremental_port);

        ~MarketDataPublisher() {
            stop();

            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(5s);

            delete snapshot_synthesizer_;
            snapshot_synthesizer_ = nullptr;
        }

        /// 开启/关闭市场数据发布器主线程（连同内部快照合成器线程）
        auto start() {
            run_ = true;

            ASSERT(Common::createAndStartThread(-1, "Exchange/MarketDataPublisher", [this](){
                run();
            }) != nullptr, "Failed to start MarketData thread.");

            snapshot_synthesizer_ -> start();
        }

        auto stop() -> void {
            run_ = false;

            snapshot_synthesizer_->stop();
        }

        /// 该线程的 main loop，消费市场更新（从无锁队列中），发布它们到增量组播流并把它们转发到快照合成器中
        auto run() noexcept -> void;

    private:
        /// 序列号用于记录增量市场数据流
        size_t next_inc_seq_num_ = 1;

        /// 无锁队列用于消费市场数据更新
        MEMarketUpdateLFQueue *outgoing_md_updates_ = nullptr;

        /// 无锁队列用于把市场增量数据的更新发送到快照合成器
        MDPMarketUpdateLFQueue snapshot_md_updates_;

        volatile bool run_ = false;

        std::string time_str_;
        Logger logger_;

        /// 市场增量数据的组播socket
        Common::McastSocket incremental_socket_;

        /// 在快照组播流上的合成器，用于生成并发布限价订单簿快照
        SnapshotSynthesizer *snapshot_synthesizer_ = nullptr;
    };
}