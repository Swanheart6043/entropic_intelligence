#include <thread>
#include <chrono>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include "bootstrap.hpp"
#include "config_parser.hpp"
#include "common/wss_connector.hpp"
#include "common/reporter_logger.hpp"
#include "cloud_downlink/downlink_dispatcher.hpp"
#include "cloud_uplink/uplink_aggregator.hpp"
#include "cloud_api/cloud_api_service.hpp"
#include "mode_server/mode_reporter.hpp"
#include "online_reporter/online_reporter.hpp"
#include "online_reporter/info_reporter.hpp"

int main() {
    try {
        patrol_robot::init_crash_handler("web_gateway");
        patrol_robot::init_logging("web_gateway");
        reporter_logger::ReporterLogger::init();
        Config* config = Config::getInstance();
        if (!config->loadFromFile("/etc/patrol_robot_h1/config.ini")) {
            throw std::runtime_error("cannot load web_gateway configuration");
        }

        WebSocketClient::TlsConfig websocket_tls;
        websocket_tls.verify_certificate = config->get_cloud_tls_verify();
        websocket_tls.ca_cert_file = config->get_cloud_ca_cert_path();
        WebSocketClient wss_connector(config->get_websocket_url(), websocket_tls);
        CloudDownlinkDispatcher downlink_dispatcher(wss_connector);
        CloudUplinkAggregator uplink_aggregator(wss_connector);
        CloudApiService cloud_api_service(wss_connector);
        ModeReporter mode_reporter(wss_connector);
        OnlineReporter online_reporter(wss_connector);
        InfoReporter info_reporter(wss_connector);

        // 先注册下行回调，避免连接刚建立时首条云端消息落在空回调窗口。
        downlink_dispatcher.start();
        wss_connector.start_client();
        cloud_api_service.start();
        uplink_aggregator.start();
        mode_reporter.start();
        online_reporter.start();
        info_reporter.start();

        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
        return 0;
    } catch (const std::exception& e) {
        SPDLOG_CRITICAL("Web网关异常: {}", e.what());
        return -1;
    }
}
