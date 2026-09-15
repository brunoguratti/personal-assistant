#pragma once

class StatusWebServer {
public:
    static StatusWebServer& GetInstance();

    void Start();

private:
    StatusWebServer() = default;
};