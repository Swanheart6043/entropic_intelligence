#pragma once
#include <string>
#include <unordered_map>
#include "config_parser.hpp"

namespace i18n {
    enum class Msg {
        OperationSuccess,
        InternalError,
        InvalidParameter,
        QuerySuccess,
        IncorrectAccountPassword,
    };

    inline std::string current_lang() {
        return Config::getInstance()->get_language();
    }

    inline const std::string& text(Msg key) {
        static const std::unordered_map<Msg, std::unordered_map<std::string, std::string>> table = {
            {Msg::OperationSuccess, {{"zh", "操作成功"}, {"en", "Operation successful"}}},
            {Msg::InternalError, {{"zh", "内部错误"}, {"en", "Internal error"}}},
            {Msg::InvalidParameter, {{"zh", "参数不正确"}, {"en", "Invalid parameter"}}},
            {Msg::QuerySuccess, {{"zh", "查询成功"}, {"en", "Query successful"}}},
            {Msg::IncorrectAccountPassword, {{"zh", "账号密码错误"}, {"en", "Incorrect account password"}}},
        };
        static const std::string fallback = "unknown";

        const std::string lang = current_lang();
        auto it = table.find(key);
        if (it == table.end()) return fallback;
        auto lit = it->second.find(lang);
        if (lit != it->second.end()) return lit->second;
        auto zh = it->second.find("zh");
        return zh != it->second.end() ? zh->second : fallback;
    }
}