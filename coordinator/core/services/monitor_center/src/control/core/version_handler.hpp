#pragma once
#include <string>

class VersionHandler {
public: 
    VersionHandler();
    ~VersionHandler();
    void save(const std::string& version);

private:
    bool need_save = true;
};