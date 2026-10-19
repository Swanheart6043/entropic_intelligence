#pragma once
#include <stdexcept>
#include "i18n.hpp"

class HttpError : public std::runtime_error {
public:
    HttpError(int status, i18n::Msg msg) : std::runtime_error("http_error"), status_(status), msg_(msg) {}
    int status() const { return status_; }
    i18n::Msg msg() const { return msg_; }
private:
    int status_;
    i18n::Msg msg_;
};

// 便捷构造：参数非法
inline HttpError bad_request(i18n::Msg msg = i18n::Msg::InvalidParameter) {
    return HttpError(400, msg);
}