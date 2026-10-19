#pragma once
#include <string>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include "http_error.hpp"

// 必填字符串，缺失或空 → 400
inline std::string require_string(const nlohmann::json& body, const char* key) {
    if (!body.contains(key) || !body.at(key).is_string()) {
        throw bad_request();
    }
    std::string v = body.at(key).get<std::string>();
    if (v.empty()) {
        throw bad_request();
    }
    return v;
}

// 枚举校验，不在集合内 → 400
inline void require_in(const std::string& value, const std::unordered_set<std::string>& allowed) {
    if (allowed.find(value) == allowed.end()) {
        throw bad_request();
    }
}