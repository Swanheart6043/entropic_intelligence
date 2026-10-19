#pragma once
#include <string>
#include <unordered_map>

class DBSettings {
public:
    static std::string get(const std::string& key, const std::string& default_val = "");
    static bool set(const std::string& key, const std::string& value);
    static std::unordered_map<std::string, std::string> get_all();
};