#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gincy {
struct Statement {
    std::string sql;
    std::vector<std::string> params;
    bool raw = false;
};
struct Job {
    std::uint64_t id;
    std::vector<Statement> statements;
};
using Row = std::map<std::string, std::optional<std::string>>;
struct Result {
    std::uint64_t id = 0;
    bool ok = false;
    std::string code;
    std::vector<Row> rows;
    double milliseconds = 0;
};
struct Config {
    std::map<std::string, std::string> connection;
    unsigned workers = 2;
    unsigned capacity = 256;
    unsigned timeoutMs = 5000;
};
class Pool {
public:
    explicit Pool(Config config);
    ~Pool();
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    std::uint64_t submit(std::vector<Statement> statements);
    std::vector<Result> poll(unsigned limit);
    std::size_t pending() const;
    void shutdown();
private:
    void worker();
    Config config_;
    std::atomic<bool> stopping_{false};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::deque<Result> results_;
    std::vector<std::thread> workers_;
    std::uint64_t sequence_ = 0;
    std::size_t pending_ = 0;
};
}
