#include "pool.hpp"
#include <libpq-fe.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <utility>
#include <cctype>
#include <cerrno>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

namespace gincy {
namespace {
using Clock = std::chrono::steady_clock;
using Connection = std::unique_ptr<PGconn, decltype(&PQfinish)>;
using QueryResult = std::unique_ptr<PGresult, decltype(&PQclear)>;
struct Failure : std::runtime_error {
    explicit Failure(const std::string& code) : std::runtime_error(code) {}
};
void waitSocket(PGconn* connection, bool writing, Clock::time_point deadline, const std::atomic<bool>& stopping) {
#ifdef _WIN32
    const SOCKET socket = static_cast<SOCKET>(PQsocket(connection));
    if (socket == INVALID_SOCKET) throw Failure("CONNECTION_REFUSED");
    const int count = 0;
#else
    const int socket = PQsocket(connection);
    if (socket < 0 || socket >= FD_SETSIZE) throw Failure("CONFIGURATION_ERROR");
    const int count = socket + 1;
#endif
    while (true) {
        if (stopping || Clock::now() >= deadline) throw Failure("TIMEOUT");
        fd_set descriptors;
        fd_set failures;
        FD_ZERO(&descriptors);
        FD_ZERO(&failures);
        FD_SET(socket, &descriptors);
        FD_SET(socket, &failures);
        timeval timeout{0, 20000};
        // Winsock reports a failed nonblocking connect only through exceptfds.
        const auto status = select(count, writing ? nullptr : &descriptors,
            writing ? &descriptors : nullptr, &failures, &timeout);
        if (status > 0) {
#ifdef _WIN32
            if (FD_ISSET(socket, &failures)) {
                int error = 0;
                int length = sizeof(error);
                getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
                if (error == WSAETIMEDOUT) throw Failure("TIMEOUT");
                if (error == WSAECONNREFUSED) throw Failure("CONNECTION_REFUSED");
                throw Failure("CONNECTION_ERROR");
            }
#endif
            return;
        }
        if (status < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            throw Failure("CONNECTION_REFUSED");
        }
    }
}
std::string connectionFailure(PGconn* connection) {
    std::string message = connection ? PQerrorMessage(connection) : "";
    std::transform(message.begin(), message.end(), message.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (message.find("password authentication failed") != std::string::npos || message.find("no password supplied") != std::string::npos || message.find("pg_hba.conf") != std::string::npos) return "AUTHENTICATION_FAILED";
    if (message.find("database") != std::string::npos && message.find("does not exist") != std::string::npos) return "DATABASE_NOT_FOUND";
    if (message.find("ssl") != std::string::npos || message.find("certificate") != std::string::npos) return "SSL_ERROR";
    if (message.find("timeout") != std::string::npos || message.find("timed out") != std::string::npos) return "TIMEOUT";
    if (message.find("invalid") != std::string::npos || message.find("translate host name") != std::string::npos) return "CONFIGURATION_ERROR";
    if (message.find("refused") != std::string::npos) return "CONNECTION_REFUSED";
    return "CONNECTION_ERROR";
}
Connection connect(const Config& config, const std::atomic<bool>& stopping) {
    std::vector<const char*> keys;
    std::vector<const char*> values;
    for (const auto& entry : config.connection) {
        keys.push_back(entry.first.c_str());
        values.push_back(entry.second.c_str());
    }
    keys.push_back(nullptr);
    values.push_back(nullptr);
    Connection connection(PQconnectStartParams(keys.data(), values.data(), 0), PQfinish);
    if (!connection) throw Failure("CONNECTION_REFUSED");
    const auto deadline = Clock::now() + std::chrono::milliseconds(config.timeoutMs);
    while (true) {
        const auto status = PQconnectPoll(connection.get());
        if (status == PGRES_POLLING_OK) break;
        if (status == PGRES_POLLING_FAILED) throw Failure(connectionFailure(connection.get()));
        waitSocket(connection.get(), status == PGRES_POLLING_WRITING, deadline, stopping);
    }
    if (PQsetnonblocking(connection.get(), 1) != 0) throw Failure("CONNECTION_REFUSED");
    return connection;
}
std::vector<Row> receive(PGconn* connection, const Config& config, const std::atomic<bool>& stopping) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(config.timeoutMs);
    while (true) {
        const int status = PQflush(connection);
        if (status == 0) break;
        if (status < 0) throw Failure("CONNECTION_REFUSED");
        waitSocket(connection, true, deadline, stopping);
    }
    std::vector<Row> rows;
    std::size_t bytes = 0;
    std::string failure;
    bool received = false;
    while (true) {
        while (PQisBusy(connection)) {
            waitSocket(connection, false, deadline, stopping);
            if (!PQconsumeInput(connection)) throw Failure("CONNECTION_REFUSED");
        }
        QueryResult result(PQgetResult(connection), PQclear);
        if (!result) break;
        received = true;
        const auto status = PQresultStatus(result.get());
        if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
            const char* code = PQresultErrorField(result.get(), PG_DIAG_SQLSTATE);
            failure = code ? code : "QUERY_ERROR";
            const char* message = PQresultErrorField(result.get(), PG_DIAG_MESSAGE_PRIMARY);
            if (failure == "P0001" && message) {
                const std::string applicationCode(message);
                if (!applicationCode.empty() && applicationCode.size() <= 64 && applicationCode.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_") == std::string::npos) failure += ":" + applicationCode;
            }
            continue;
        }
        for (int rowIndex = 0; rowIndex < PQntuples(result.get()); ++rowIndex) {
            if (rows.size() >= 2048) throw Failure("RESULT_LIMIT");
            Row row;
            for (int column = 0; column < PQnfields(result.get()); ++column) {
                if (PQgetisnull(result.get(), rowIndex, column)) {
                    row.emplace(PQfname(result.get(), column), std::nullopt);
                } else {
                    const int length = PQgetlength(result.get(), rowIndex, column);
                    bytes += static_cast<std::size_t>(length);
                    if (bytes > 4 * 1024 * 1024) throw Failure("RESULT_LIMIT");
                    row.emplace(PQfname(result.get(), column), std::string(PQgetvalue(result.get(), rowIndex, column), length));
                }
            }
            rows.push_back(std::move(row));
        }
    }
    if (!received) throw Failure("PROTOCOL");
    if (!failure.empty()) throw Failure(failure);
    return rows;
}
std::vector<Row> raw(PGconn* connection, const std::string& sql, const Config& config, const std::atomic<bool>& stopping) {
    if (!PQsendQuery(connection, sql.c_str())) throw Failure("CONNECTION_REFUSED");
    return receive(connection, config, stopping);
}
}
Pool::Pool(Config config) : config_(std::move(config)) {
    config_.workers = std::clamp(config_.workers, 1U, 8U);
    config_.capacity = std::clamp(config_.capacity, 16U, 1024U);
    config_.timeoutMs = std::clamp(config_.timeoutMs, 1000U, 30000U);
    try {
        for (unsigned i = 0; i < config_.workers; ++i) workers_.emplace_back(&Pool::worker, this);
    } catch (...) {
        shutdown();
        throw;
    }
}
Pool::~Pool() { shutdown(); }
void Pool::shutdown() {
    stopping_ = true;
    wake_.notify_all();
    for (auto& thread : workers_) if (thread.joinable()) thread.join();
    workers_.clear();
}
std::uint64_t Pool::submit(std::vector<Statement> statements) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || pending_ >= config_.capacity) return 0;
    const auto id = ++sequence_;
    jobs_.push_back({id, std::move(statements)});
    ++pending_;
    wake_.notify_one();
    return id;
}
std::vector<Result> Pool::poll(unsigned limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Result> results;
    while (!results_.empty() && results.size() < limit) {
        results.push_back(std::move(results_.front()));
        results_.pop_front();
        --pending_;
    }
    return results;
}
std::size_t Pool::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_;
}
void Pool::worker() {
    Connection connection(nullptr, PQfinish);
    std::map<std::string, std::string> prepared;
    auto reconnectAt = Clock::time_point{};
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        Result result;
        result.id = job.id;
        const auto started = Clock::now();
        try {
            if (!connection || PQstatus(connection.get()) != CONNECTION_OK) {
                if (Clock::now() < reconnectAt) throw Failure("BACKOFF");
                connection = connect(config_, stopping_);
                prepared.clear();
            }
            raw(connection.get(), "BEGIN", config_, stopping_);
            for (const auto& statement : job.statements) {
                if (statement.raw) {
                    result.rows = raw(connection.get(), statement.sql, config_, stopping_);
                    continue;
                }
                auto entry = prepared.find(statement.sql);
                if (entry == prepared.end()) {
                    if (prepared.size() >= 256) {
                        raw(connection.get(), "DEALLOCATE ALL", config_, stopping_);
                        prepared.clear();
                    }
                    const std::string name = "gincy_" + std::to_string(prepared.size());
                    if (!PQsendPrepare(connection.get(), name.c_str(), statement.sql.c_str(),
                        static_cast<int>(statement.params.size()), nullptr)) throw Failure("CONNECTION_REFUSED");
                    receive(connection.get(), config_, stopping_);
                    entry = prepared.emplace(statement.sql, name).first;
                }
                std::vector<const char*> params;
                for (const auto& value : statement.params) params.push_back(value.c_str());
                if (!PQsendQueryPrepared(connection.get(), entry->second.c_str(), static_cast<int>(params.size()),
                    params.data(), nullptr, nullptr, 0)) throw Failure("CONNECTION_REFUSED");
                result.rows = receive(connection.get(), config_, stopping_);
            }
            raw(connection.get(), "COMMIT", config_, stopping_);
            result.ok = true;
        } catch (const Failure& error) {
            result.code = error.what();
            result.rows.clear();
            bool recovered = false;
            if ((result.code.size() == 5 || result.code.rfind("P0001:", 0) == 0) && connection && PQstatus(connection.get()) == CONNECTION_OK) {
                try {
                    raw(connection.get(), "ROLLBACK", config_, stopping_);
                    recovered = true;
                } catch (...) {
                    recovered = false;
                }
            }
            if (result.code.rfind("P0001:", 0) == 0) result.code = result.code.substr(6);
            else if (result.code == "28P01" || result.code == "28000") result.code = "AUTHENTICATION_FAILED";
            else if (result.code == "3D000") result.code = "DATABASE_NOT_FOUND";
            else if (result.code == "57014") result.code = "TIMEOUT";
            else if (result.code.size() == 5) result.code = "QUERY_ERROR:" + result.code;
            if (!recovered) {
                connection.reset();
                prepared.clear();
                reconnectAt = Clock::now() + std::chrono::milliseconds(500);
            }
        } catch (...) {
            result.code = "NATIVE_FAILURE";
            result.rows.clear();
            connection.reset();
            prepared.clear();
            reconnectAt = Clock::now() + std::chrono::milliseconds(500);
        }
        result.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        std::lock_guard<std::mutex> lock(mutex_);
        results_.push_back(std::move(result));
    }
}
}
