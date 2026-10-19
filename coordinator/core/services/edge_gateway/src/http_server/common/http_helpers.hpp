#pragma once
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "httplib.hpp"
#include "i18n.hpp"
#include "http_error.hpp"

namespace http_helpers {
    // 把 body 写进 res,统一设置 content-type 和状态码
    inline void write_json(httplib::Response& res, std::string body, int status = 200) {
        res.status = status;
        res.set_content(std::move(body), "application/json");
    }

    inline std::string ok_body(nlohmann::json result = nullptr, i18n::Msg msg = i18n::Msg::OperationSuccess) {
        return nlohmann::json{
            {"success", true}, 
            {"message", i18n::text(msg)},  // 修复:序列化文本而非枚举整数
            {"code", 200}, 
            {"result", std::move(result)}
        }.dump();
    }

    inline std::string error_body(int code, const std::string& msg) {
        return nlohmann::json{
            {"success", false}, 
            {"message", msg},
            {"code", code}, 
            {"result", ""}
        }.dump();
    }

    inline std::string success(i18n::Msg m = i18n::Msg::OperationSuccess) {
        return nlohmann::json{
            {"success", true},
            {"message", i18n::text(m)},
            {"code", 200},
            {"result", nullptr}
        }.dump();
    }
    
    inline std::string fail_body(int code, i18n::Msg m) {
        return nlohmann::json{
            {"success", false},
            {"message", i18n::text(m)},
            {"code", code},
            {"result", nullptr}
        }.dump();
    }

    // // 包一层异常处理:controller 抛 json::parse 异常时返回 400,不会断连接
    // // h 的签名:std::string(const httplib::Request&) —— 返回已 dump 的 json 字符串
    // template <typename Handler>
    // httplib::Server::Handler safe(Handler&& h) {
    //     return [h = std::forward<Handler>(h)](const httplib::Request& req, httplib::Response& res) {
    //         try {
    //             write_json(res, h(req), 200);
    //         } catch (const nlohmann::json::exception& e) {
    //             write_json(res, error_body(400, std::string("请求体解析失败: ") + e.what()), 400);
    //         } catch (const std::exception& e) {
    //             write_json(res, error_body(500, e.what()), 500);
    //         }
    //     };
    // }

    template <typename Handler>
    httplib::Server::Handler safe(Handler&& h) {
        return [h = std::forward<Handler>(h)](const httplib::Request& req, httplib::Response& res) {
            try {
                using Ret = std::invoke_result_t<Handler&, const httplib::Request&>;

                if constexpr (std::is_same_v<Ret, std::string>) {
                    // 旧契约:handler 自己 dump 好完整响应,直接写
                    write_json(res, h(req), 200);
                } else {
                    // 新契约:handler 返回业务 json,wrapper 套 ok_body
                    nlohmann::json data = h(req);
                    write_json(res, ok_body(std::move(data), i18n::Msg::QuerySuccess), 200);
                }
            } catch (const HttpError& e) {
                // 业务主动抛的可预期错误
                SPDLOG_INFO("请求返回错误: status={}", e.status());
                write_json(res, fail_body(e.status(), e.msg()), e.status());
            } catch (const nlohmann::json::exception& e) {
                SPDLOG_INFO("请求体 JSON 异常: {}", e.what());
                write_json(res, fail_body(400, i18n::Msg::InvalidParameter), 200);
                // write_json(res, ok_body("", i18n::Msg::QuerySuccess), 200);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("[{}] 未捕获异常: {}", req.path, e.what());
                // write_json(res, fail_body(500, i18n::Msg::InternalError), 500);
                write_json(res, ok_body("", i18n::Msg::QuerySuccess), 200);
            }
        };
    }
}