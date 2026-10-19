#pragma once
#include "httplib.hpp"

class YueChuAnXun {
public: 
    YueChuAnXun();
    void control(const std::string& location, const std::string& type);
private:
    httplib::Client http{"19.25.4.208", 8080};
    const httplib::Headers headers = {{"X-API-KEY", "uTJ90KCHRDmJroMAwLeeKA"}};
};