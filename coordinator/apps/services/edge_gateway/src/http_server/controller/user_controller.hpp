#pragma once
#include <string>
#include <iostream>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "../common/i18n.hpp"

class UserController {
public:
    std::string login(const httplib::Request&);

private:
    static std::string success(i18n::Msg m = i18n::Msg::OperationSuccess) {
        return nlohmann::json{
            {"success", true},
            {"message", i18n::text(m)},
            {"code", 200},
            {"result", nullptr}
        }.dump();
    }
    static std::string fail(int code, i18n::Msg m) {
        return nlohmann::json{
            {"success", false},
            {"message", i18n::text(m)},
            {"code", code},
            {"result", nullptr}
        }.dump();
    }
};