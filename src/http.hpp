#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace gincy {
struct HttpRequest {
    std::uint64_t id = 0;
    std::string method;
    std::string path;
    std::string body;
    std::string authorization;
    std::string origin;
    std::string host;
};
struct HttpConfig {
    std::string host = "127.0.0.1";
    unsigned port = 27491;
    std::string token;
    std::string webRoot;
    unsigned managementApi = 1;
    std::string product = "3.2.0";
};
bool httpStart(const HttpConfig& config, std::string& error);
void httpStop();
bool httpRunning();
std::vector<HttpRequest> httpPoll(unsigned limit);
void httpReply(std::uint64_t id, int status, const std::string& contentType, const std::string& body);
unsigned httpPort();
}
