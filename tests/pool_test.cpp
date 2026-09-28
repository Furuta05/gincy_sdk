#include "pool.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
gincy::Result wait(gincy::Pool& pool) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        auto results = pool.poll(1);
        if (!results.empty()) return std::move(results[0]);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("result timeout");
}
int main() {
    try {
        gincy::Config config;
        config.workers = 1;
        config.capacity = 16;
        // Windows retries SYN to a closed loopback port for ~2 s before reporting WSAECONNREFUSED.
        config.timeoutMs = 5000;
        config.connection = {{"host", "127.0.0.1"}, {"port", "1"}, {"dbname", "postgres"}, {"user", "postgres"}, {"sslmode", "disable"}};
        if (const auto* password = std::getenv("GINCY_TEST_PASSWORD")) config.connection["password"] = password;
        if (const auto* user = std::getenv("GINCY_TEST_USER")) config.connection["user"] = user;
        if (const auto* port = std::getenv("GINCY_TEST_PORT")) config.connection["port"] = port;
        gincy::Pool pool(config);
        if (const auto* path = std::getenv("GINCY_TEST_MIGRATION")) {
            std::ifstream input(path);
            check(input.good(), "migration file unavailable");
            std::ostringstream sql;
            sql << input.rdbuf();
            check(pool.submit({{sql.str(), {}, true}}) != 0, "migration submit failed");
            auto migrated = wait(pool);
            check(migrated.ok, migrated.code.c_str());
        }
        const auto start = std::chrono::steady_clock::now();
        check(pool.submit({{"SELECT $1::text AS value", {"quote'; DROP TABLE anything;"}}}) != 0, "submit failed");
        check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50), "submit blocked");
        auto result = wait(pool);
        if (std::getenv("GINCY_TEST_PORT")) {
            check(result.ok, result.code.c_str());
            check(result.rows.at(0).at("value").value() == "quote'; DROP TABLE anything;", "parameter changed");
            check(pool.submit({{"CREATE TABLE gincy_success(value integer)", {}, true}, {"INSERT INTO gincy_success VALUES($1::integer)", {"42"}}, {"SELECT value::text FROM gincy_success", {}}}) != 0, "submit failed");
            auto committed = wait(pool);
            check(committed.ok && !committed.rows.empty() && committed.rows.at(0).at("value").value() == "42", "prepared transaction failed");
            if (!std::getenv("GINCY_PGLITE_TEST")) {
            check(pool.submit({{"CREATE TABLE IF NOT EXISTS gincy_pool_probe(value integer)", {}, true}, {"INSERT INTO gincy_pool_probe VALUES(1)", {}}, {"SELECT 1/0", {}}}) != 0, "submit failed");
            check(!wait(pool).ok, "transaction unexpectedly succeeded");
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            check(pool.submit({{"SELECT to_regclass('gincy_pool_probe')::text AS name", {}}}) != 0, "submit failed");
            auto rollback = wait(pool);
            check(rollback.ok, rollback.code.c_str());
            check(!rollback.rows.empty(), "rollback verification returned no rows");
            check(!rollback.rows.at(0).at("name").has_value(), "transaction did not roll back");
            check(pool.submit({{"SELECT 42::bigint AS value", {}}}) != 0, "submit failed");
            auto recovered = wait(pool);
            check(recovered.ok, recovered.code.c_str());
            check(!recovered.rows.empty() && recovered.rows.at(0).at("value").value() == "42", "query after rollback failed");
            }
        } else {
            check(!result.ok && result.code == "CONNECTION_REFUSED", "connection failure not reported");
        }
        for (unsigned i = 0; i < 16; ++i) check(pool.submit({{"SELECT 1", {}}}) != 0, "queue rejected early");
        check(pool.submit({{"SELECT 1", {}}}) == 0, "queue limit failed");
        for (unsigned i = 0; i < 16; ++i) wait(pool);
        check(pool.pending() == 0, "pending counter leaked");
        pool.shutdown();
        check(pool.submit({{"SELECT 1", {}}}) == 0, "submit after shutdown accepted");
        std::cout << "PASS native pool: nonblocking submit, bounded queue, results, shutdown" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << std::endl;
        return 1;
    }
}
