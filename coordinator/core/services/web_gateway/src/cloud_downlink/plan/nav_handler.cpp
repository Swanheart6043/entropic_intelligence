#include <spdlog/spdlog.h>
#include "nav_handler.hpp"

namespace {
    const nlohmann::json kNavError = {
        {"data", ""},
        {"errorCode", "500"},
        {"msg", "Navigation error"},
        {"successed", false},
    };
}

HighFreedomNavigator::HighFreedomNavigator(WebSocketClient& websocket) : websocket_(websocket), fast_("10.10.10.10", 8080), slow_("10.10.10.10", 8080) {
    fast_.http.set_connection_timeout(0, 300 * 1000); // 300ms
    fast_.http.set_read_timeout(0, 1000 * 1000);        // 1000ms
    fast_.http.set_write_timeout(0, 1000 * 1000);
    fast_.http.set_keep_alive(true);

    slow_.http.set_connection_timeout(0, 300 * 1000); // 300ms
    slow_.http.set_read_timeout(20, 0);                // 20s
    slow_.http.set_write_timeout(0, 500 * 1000);
    slow_.http.set_keep_alive(true);

    fast_.worker = std::thread(&HighFreedomNavigator::worker_loop, this, std::ref(fast_));
    slow_.worker = std::thread(&HighFreedomNavigator::worker_loop, this, std::ref(slow_));
}

HighFreedomNavigator::~HighFreedomNavigator() {
    {
        std::lock_guard<std::mutex> lf(fast_.mutex);
        std::lock_guard<std::mutex> ls(slow_.mutex);
        running_ = false;
    }
    fast_.cv.notify_all();
    slow_.cv.notify_all();
    if (fast_.worker.joinable()) fast_.worker.join();
    if (slow_.worker.joinable()) slow_.worker.join();
}

// 收到消息后判断，入队
void HighFreedomNavigator::submit(const std::string& message) {
    // 轻量解析:取 url 决定通道,取 uuid 备用于繁忙应答
    std::string url;
    nlohmann::json uuid;
    try {
        auto request = nlohmann::json::parse(message).at("Request");
        url = request.at("Url").get<std::string>();
        uuid = request.at("Uuid");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("submit 解析失败: {}", e.what());
        return; // 连 url/uuid 都取不到,丢弃
    }

    Channel& ch = is_slow_url(url) ? slow_ : fast_;
    {
        std::lock_guard<std::mutex> lock(ch.mutex);
        if (ch.queue.size() < kMaxQueue) {
            ch.queue.push_back(message);
            ch.cv.notify_one();
            return;
        }
    }
    SPDLOG_WARN("导航队列已满({}) url={}", kMaxQueue, url);
    reply(uuid);
}

// 两个线程共用，出队
void HighFreedomNavigator::worker_loop(Channel& ch) {
    while (true) {
        std::string message;
        {
            std::unique_lock<std::mutex> lock(ch.mutex);
            ch.cv.wait(lock, [this, &ch] { return !running_ || !ch.queue.empty(); });
            if (!running_ && ch.queue.empty()) return;
            message = std::move(ch.queue.front());
            ch.queue.pop_front();
        }
        process(ch, message);
    }
}

// 处理业务
void HighFreedomNavigator::process(Channel& ch, const std::string& message) {
    using namespace nlohmann;
    json uuid;
    try {
        json request = json::parse(message).at("Request");
        std::string method = request.at("Method").get<std::string>();
        std::string url = request.at("Url").get<std::string>();
        std::string param = request.value("Param", "");
        uuid = request.at("Uuid");

        httplib::Result response;
        if (method == "GET") {
            response = ch.http.Get(url);
        } else if (method == "POST") {
            response = ch.http.Post(url, param, "application/json");
        } else {
            reply(uuid);
            return;
        }

        if (response && response->status == 200) {
            reply(uuid, parse_body(response->body));
        } else {
            auto text = response ? std::to_string(response->status) : httplib::to_string(response.error());
            SPDLOG_ERROR("导航请求失败: {}", text);
            reply(uuid);
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("导航处理异常: {}", e.what());
        if (!uuid.is_null()) reply(uuid);
    }
}

// 上报应答
void HighFreedomNavigator::reply(const nlohmann::json& uuid, nlohmann::json result) {
    if (result.is_null()) result = kNavError;

    nlohmann::json message = {
        {"Header", {
            {"CmdType", "RC_Robot_Gs_Data_Get"},
            {"Sn", config_->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"NavInfo", {
            {"Result", result},
            {"Uuid", uuid}
        }}
    };
    std::string payload = message.dump();
    SPDLOG_DEBUG("导航应答: {}", payload);

    std::lock_guard<std::mutex> lock(send_mutex_);
    websocket_.send_message(payload);
}

nlohmann::json HighFreedomNavigator::parse_body(const std::string& body) {
    if (body.empty()) {
        SPDLOG_WARN("导航响应 body 为空");
        return kNavError;
    }
    auto result = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (result.is_discarded()) {
        SPDLOG_WARN("导航响应非 JSON: {}", body.substr(0, 200));
        return kNavError;
    }
    return result;
}

bool HighFreedomNavigator::is_slow_url(const std::string& url) const {
    return url == "/gs-robot/cmd/initialize" || url == "/gs-robot/cmd/initialize_customized";
}