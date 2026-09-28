#include "http.hpp"
#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <condition_variable>
#include <chrono>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
static void closeSocket(Socket socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
static const Socket INVALID_SOCKET = -1;
static void closeSocket(Socket socket) { ::close(socket); }
#endif
namespace gincy {
namespace {
std::mutex mutex;
std::condition_variable replied;
HttpConfig config;
bool running = false;
Socket listener = INVALID_SOCKET;
std::thread worker;
std::uint64_t nextId = 1;
unsigned boundPort = 0;
struct Pending {
    Socket socket = INVALID_SOCKET;
    bool answered = false;
    int status = 500;
    std::string type = "application/json";
    std::string body;
};
std::map<std::uint64_t, Pending> pending;
std::vector<HttpRequest> inbox;
std::map<std::string, std::chrono::steady_clock::time_point> lastHit;
unsigned hits = 0;
std::chrono::steady_clock::time_point window = std::chrono::steady_clock::now();
const char* mime(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html; charset=utf-8";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "text/javascript";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") return "application/json";
    return "application/octet-stream";
}
std::string fileName(const std::string& path) {
    if (path == "/" || path == "/index.html") return "index.html";
    if (path == "/app.js") return "app.js";
    if (path == "/style.css") return "style.css";
    if (path == "/ui-version.json") return "ui-version.json";
    return {};
}
void sendAll(Socket socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        int n = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
        int n = static_cast<int>(send(socket, data.data() + sent, data.size() - sent, 0));
#endif
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
}
void respond(Socket socket, int status, const std::string& type, const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " \r\nContent-Type: " << type
        << "\r\nContent-Length: " << body.size()
        << "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff"
        << "\r\nReferrer-Policy: no-referrer"
        << "\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"
        << "\r\nConnection: close\r\n\r\n" << body;
    sendAll(socket, out.str());
}
bool limited() {
    auto now = std::chrono::steady_clock::now();
    if (now - window >= std::chrono::seconds(1)) { window = now; hits = 0; }
    return ++hits > 32;
}
std::string readFile(const std::string& name) {
    std::ifstream input(config.webRoot + "/" + name, std::ios::binary);
    if (!input) return {};
    return std::string(std::istreambuf_iterator<char>(input), {});
}
void handle(Socket socket) {
    std::string raw;
    char buffer[4096];
    while (raw.size() < 262144) {
#ifdef _WIN32
        int n = recv(socket, buffer, sizeof(buffer), 0);
#else
        int n = static_cast<int>(recv(socket, buffer, sizeof(buffer), 0));
#endif
        if (n <= 0) break;
        raw.append(buffer, static_cast<std::size_t>(n));
        if (raw.find("\r\n\r\n") != std::string::npos) break;
    }
    auto headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) { respond(socket, 400, "application/json", "{\"error\":\"BAD_REQUEST\"}"); closeSocket(socket); return; }
    std::istringstream stream(raw.substr(0, headerEnd));
    std::string method, path, version, line;
    stream >> method >> path >> version;
    std::map<std::string, std::string> headers;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon), value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        headers[key] = value;
    }
    std::size_t length = 0;
    if (headers.count("content-length")) length = static_cast<std::size_t>(std::strtoul(headers["content-length"].c_str(), nullptr, 10));
    if (length > 16 * 1024 * 1024) { respond(socket, 413, "application/json", "{\"error\":\"TOO_LARGE\"}"); closeSocket(socket); return; }
    std::string body = raw.substr(headerEnd + 4);
    while (body.size() < length) {
#ifdef _WIN32
        int n = recv(socket, buffer, sizeof(buffer), 0);
#else
        int n = static_cast<int>(recv(socket, buffer, sizeof(buffer), 0));
#endif
        if (n <= 0) break;
        body.append(buffer, static_cast<std::size_t>(n));
    }
    body.resize(std::min(body.size(), length));
    if (limited()) { respond(socket, 429, "application/json", "{\"error\":\"RATE\"}"); closeSocket(socket); return; }
    if (method == "GET" && path == "/api/meta") {
        Json meta = Json::object();
        meta.set("product", Json::string(config.product));
        meta.set("management_api", Json::integer(config.managementApi));
        respond(socket, 200, "application/json", meta.dump());
        closeSocket(socket);
        return;
    }
    if (method == "GET") {
        auto name = fileName(path);
        if (name.empty()) { respond(socket, 404, "application/json", "{\"error\":\"NOT_FOUND\"}"); closeSocket(socket); return; }
        auto bytes = readFile(name);
        if (bytes.empty()) { respond(socket, 404, "application/json", "{\"error\":\"WEBUI_MISSING\"}"); closeSocket(socket); return; }
        respond(socket, 200, mime(name), bytes);
        closeSocket(socket);
        return;
    }
    if (method != "POST" || (path != "/api/execute" && path != "/api/package")) {
        respond(socket, 404, "application/json", "{\"error\":\"NOT_FOUND\"}");
        closeSocket(socket);
        return;
    }
    const std::string expected = "Bearer " + config.token;
    if (headers["authorization"] != expected) { respond(socket, 401, "application/json", "{\"error\":\"UNAUTHORIZED\"}"); closeSocket(socket); return; }
    std::string expectedOrigin = std::string("http://") + headers["host"];
    if (!headers["origin"].empty() && headers["origin"] != expectedOrigin) { respond(socket, 403, "application/json", "{\"error\":\"ORIGIN\"}"); closeSocket(socket); return; }
    if (pending.size() >= 8) { respond(socket, 503, "application/json", "{\"error\":\"BUSY\"}"); closeSocket(socket); return; }
    HttpRequest request;
    {
        std::lock_guard<std::mutex> lock(mutex);
        request.id = nextId++;
        request.method = method;
        request.path = path;
        request.body = std::move(body);
        request.authorization = headers["authorization"];
        request.origin = headers["origin"];
        request.host = headers["host"];
        pending[request.id] = Pending{socket};
        inbox.push_back(request);
    }
    std::unique_lock<std::mutex> lock(mutex);
    auto done = replied.wait_for(lock, std::chrono::seconds(15), [&] {
        auto it = pending.find(request.id);
        return it == pending.end() || it->second.answered;
    });
    auto it = pending.find(request.id);
    if (!done || it == pending.end() || !it->second.answered) {
        if (it != pending.end()) {
            respond(it->second.socket, 504, "application/json", "{\"error\":\"TIMEOUT\"}");
            closeSocket(it->second.socket);
            pending.erase(it);
        }
        return;
    }
    respond(it->second.socket, it->second.status, it->second.type, it->second.body);
    closeSocket(it->second.socket);
    pending.erase(it);
}
void loop() {
    while (running) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(listener, &set);
        timeval timeout{0, 200000};
#ifdef _WIN32
        int ready = select(0, &set, nullptr, nullptr, &timeout);
#else
        int ready = select(listener + 1, &set, nullptr, nullptr, &timeout);
#endif
        if (ready <= 0) continue;
        Socket client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        std::thread(handle, client).detach();
    }
}
}
bool httpStart(const HttpConfig& next, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex);
    if (running) { error = "already running"; return false; }
    if (next.token.size() < 32) { error = "token too short"; return false; }
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { error = "winsock"; return false; }
#endif
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) { error = "socket"; return false; }
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(next.port));
    if (inet_pton(AF_INET, next.host.c_str(), &addr.sin_addr) != 1) { error = "bind address"; return false; }
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { error = "bind failed"; closeSocket(listener); listener = INVALID_SOCKET; return false; }
    if (listen(listener, 16) != 0) { error = "listen failed"; closeSocket(listener); listener = INVALID_SOCKET; return false; }
    sockaddr_in actual{};
#ifdef _WIN32
    int size = sizeof(actual);
#else
    socklen_t size = sizeof(actual);
#endif
    getsockname(listener, reinterpret_cast<sockaddr*>(&actual), &size);
    boundPort = ntohs(actual.sin_port);
    config = next;
    running = true;
    worker = std::thread(loop);
    return true;
}
void httpStop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    }
    if (listener != INVALID_SOCKET) { closeSocket(listener); listener = INVALID_SOCKET; }
    if (worker.joinable()) worker.join();
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& entry : pending) closeSocket(entry.second.socket);
    pending.clear();
    inbox.clear();
#ifdef _WIN32
    WSACleanup();
#endif
}
bool httpRunning() { std::lock_guard<std::mutex> lock(mutex); return running; }
std::vector<HttpRequest> httpPoll(unsigned limit) {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<HttpRequest> out;
    limit = std::min(limit, 8u);
    while (!inbox.empty() && out.size() < limit) {
        out.push_back(std::move(inbox.front()));
        inbox.erase(inbox.begin());
    }
    return out;
}
void httpReply(std::uint64_t id, int status, const std::string& contentType, const std::string& body) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = pending.find(id);
    if (it == pending.end()) return;
    it->second.answered = true;
    it->second.status = status;
    it->second.type = contentType;
    it->second.body = body;
    replied.notify_all();
}
unsigned httpPort() { return boundPort; }
}
