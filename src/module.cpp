#include "pool.hpp"
#include "package.hpp"
#include "http.hpp"
#include "gvm.hpp"
#include "gincy_version.hpp"
#include <GarrysMod/Lua/Interface.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <openssl/crypto.h>

namespace {
using GarrysMod::Lua::ILuaBase;
std::unique_ptr<gincy::Pool> pool;
std::string readString(ILuaBase* lua, int index, std::size_t maximum) {
    if (!lua->IsType(index, GarrysMod::Lua::Type::String)) throw std::runtime_error("expected string");
    unsigned length = 0;
    const char* value = lua->GetString(index, &length);
    if (length > maximum || std::string(value, length).find('\0') != std::string::npos) throw std::runtime_error("invalid string length");
    return std::string(value, length);
}
unsigned option(ILuaBase* lua, const char* key, unsigned fallback, unsigned maximum) {
    lua->GetField(1, key);
    double value = lua->IsType(-1, GarrysMod::Lua::Type::Number) ? lua->GetNumber(-1) : fallback;
    lua->Pop();
    if (!std::isfinite(value) || value < 1 || value > maximum) throw std::runtime_error("invalid pool option");
    return static_cast<unsigned>(value);
}
int fail(ILuaBase* lua, const char* message) {
    lua->PushNil();
    lua->PushString(message);
    return 2;
}
LUA_FUNCTION(verify_package) {
    try {
        if (!LUA->IsType(1, GarrysMod::Lua::Type::String)) return fail(LUA, "expected package bytes");
        unsigned size = 0;
        const char* bytes = LUA->GetString(1, &size);
        if (size > 16 * 1024 * 1024) return fail(LUA, "package too large");
        const auto result = gincy::verifyPackage(std::string(bytes, size), readString(LUA, 2, 4096));
        LUA->CreateTable();
        LUA->PushString(result.manifest.c_str()); LUA->SetField(-2, "manifest");
        LUA->PushString(result.signer.c_str()); LUA->SetField(-2, "signer");
        LUA->PushString(result.digest.c_str()); LUA->SetField(-2, "digest");
        LUA->CreateTable();
        unsigned index = 0;
        for (const auto& entry : result.files) {
            LUA->PushNumber(++index); LUA->CreateTable();
            LUA->PushString(entry.path.c_str()); LUA->SetField(-2, "path");
            LUA->PushNumber(entry.realm); LUA->SetField(-2, "realm");
            LUA->PushBool(entry.protectedPayload); LUA->SetField(-2, "protected");
            if (!entry.protectedPayload) { LUA->PushString(entry.data.c_str(), static_cast<unsigned>(entry.data.size())); LUA->SetField(-2, "data"); }
            LUA->SetTable(-3);
        }
        LUA->SetField(-2, "files");
        return 1;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
    catch (...) { return fail(LUA, "package verification failed"); }
}
LUA_FUNCTION(load_entry) {
    try {
        if (!LUA->IsType(1, GarrysMod::Lua::Type::String) || !LUA->IsType(5, GarrysMod::Lua::Type::Table)) return fail(LUA, "invalid load arguments");
        unsigned size = 0;
        const char* bytes = LUA->GetString(1, &size);
        if (size > 16 * 1024 * 1024) return fail(LUA, "package too large");
        const auto package = gincy::verifyPackage(std::string(bytes, size), readString(LUA, 2, 4096));
        const auto name = readString(LUA, 3, 240);
        const auto entry = std::find_if(package.files.begin(), package.files.end(), [&](const gincy::PackageFile& value) { return value.path == name; });
        if (entry == package.files.end()) return fail(LUA, "entry not found");
        auto source = gincy::decryptEntry(*entry, readString(LUA, 4, 16384), package.signerRaw);
        const char* payload = source.data();
        std::size_t payloadSize = source.size();
        if (payloadSize >= 4 && std::memcmp(payload, "GVM1", 4) == 0) {
            LUA->PushString(payload, static_cast<unsigned>(payloadSize));
            OPENSSL_cleanse(source.data(), source.size());
            return 1;
        }
        if (payloadSize >= 5 && std::memcmp(payload, "GJBC", 4) == 0 && payload[4] == '\0') {
            payload += 5;
            payloadSize -= 5;
        }
        const bool bytecode = payloadSize >= 4 && ((std::memcmp(payload, "\x1bLua", 4) == 0) || (payloadSize >= 3 && std::memcmp(payload, "\x1bLJ", 3) == 0));
        LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
        if (bytecode) {
            LUA->GetField(-1, "loadstring");
            if (!LUA->IsType(-1, GarrysMod::Lua::Type::Function)) {
                LUA->Pop();
                LUA->GetField(-1, "load");
            }
        } else {
            LUA->GetField(-1, "CompileString");
        }
        LUA->PushString(payload, static_cast<unsigned>(payloadSize));
        if (bytecode) {
            if (LUA->PCall(1, 1, 0) != 0 || !LUA->IsType(-1, GarrysMod::Lua::Type::Function)) {
                OPENSSL_cleanse(source.data(), source.size());
                return fail(LUA, "protected bytecode load failed");
            }
        } else {
            LUA->PushString(("gincy/protected/" + name).c_str());
            LUA->PushBool(false);
            if (LUA->PCall(3, 1, 0) != 0 || !LUA->IsType(-1, GarrysMod::Lua::Type::Function)) {
                OPENSSL_cleanse(source.data(), source.size());
                return fail(LUA, "protected compilation failed");
            }
        }
        OPENSSL_cleanse(source.data(), source.size());
        const int functionIndex = LUA->Top();
        LUA->GetField(functionIndex - 1, "setfenv");
        LUA->Push(functionIndex);
        LUA->Push(5);
        if (LUA->PCall(2, 1, 0) != 0) return fail(LUA, "protected environment failed");
        return 1;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
    catch (...) { return fail(LUA, "protected load failed"); }
}
LUA_FUNCTION(configure) {
    if (pool) return fail(LUA, "already configured");
    if (!LUA->IsType(1, GarrysMod::Lua::Type::Table)) return fail(LUA, "expected configuration table");
    try {
        gincy::Config config;
        for (const auto* key : {"host", "port", "dbname", "user", "password", "sslmode", "sslrootcert", "sslcert", "sslkey"}) {
            LUA->GetField(1, key);
            if (!LUA->IsType(-1, GarrysMod::Lua::Type::Nil)) config.connection[key] = readString(LUA, -1, 4096);
            LUA->Pop();
        }
        config.workers = option(LUA, "workers", 2, 8);
        config.capacity = option(LUA, "capacity", 256, 1024);
        config.timeoutMs = option(LUA, "timeout_ms", 5000, 30000);
        config.connection["connect_timeout"] = "5";
        config.connection["application_name"] = "gincy";
        config.connection["client_encoding"] = "UTF8";
        config.connection["options"] = "-c statement_timeout=" + std::to_string(config.timeoutMs) + " -c lock_timeout=2000 -c idle_in_transaction_session_timeout=10000";
        pool = std::make_unique<gincy::Pool>(std::move(config));
        LUA->PushBool(true);
        return 1;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
    catch (...) { return fail(LUA, "native configuration failed"); }
}
LUA_FUNCTION(submit) {
    if (!pool) return fail(LUA, "not configured");
    if (!LUA->IsType(1, GarrysMod::Lua::Type::Table)) return fail(LUA, "expected statements");
    try {
        std::vector<gincy::Statement> statements;
        std::size_t bytes = 0;
        for (unsigned i = 1; i <= 33; ++i) {
            LUA->PushNumber(i);
            LUA->GetTable(1);
            if (LUA->IsType(-1, GarrysMod::Lua::Type::Nil)) { LUA->Pop(); break; }
            if (i > 32 || !LUA->IsType(-1, GarrysMod::Lua::Type::Table)) throw std::runtime_error("invalid statements");
            const int entry = LUA->Top();
            gincy::Statement statement;
            LUA->GetField(entry, "sql");
            statement.sql = readString(LUA, -1, 131072);
            bytes += statement.sql.size();
            LUA->Pop();
            LUA->GetField(entry, "raw");
            statement.raw = LUA->GetBool(-1);
            LUA->Pop();
            LUA->GetField(entry, "params");
            if (LUA->IsType(-1, GarrysMod::Lua::Type::Table)) {
                const int params = LUA->Top();
                for (unsigned j = 1; j <= 65; ++j) {
                    LUA->PushNumber(j);
                    LUA->GetTable(params);
                    if (LUA->IsType(-1, GarrysMod::Lua::Type::Nil)) { LUA->Pop(); break; }
                    if (j > 64) throw std::runtime_error("too many parameters");
                    auto value = readString(LUA, -1, 65536);
                    bytes += value.size();
                    statement.params.push_back(std::move(value));
                    LUA->Pop();
                }
            }
            LUA->Pop(2);
            if (bytes > 262144) throw std::runtime_error("job too large");
            statements.push_back(std::move(statement));
        }
        if (statements.empty()) return fail(LUA, "empty transaction");
        const auto id = pool->submit(std::move(statements));
        if (!id) return fail(LUA, "queue full");
        LUA->PushNumber(static_cast<double>(id));
        return 1;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
    catch (...) { return fail(LUA, "native submission failed"); }
}
LUA_FUNCTION(poll) {
    try {
        double requested = LUA->IsType(1, GarrysMod::Lua::Type::Number) ? LUA->GetNumber(1) : 16;
        if (!std::isfinite(requested)) requested = 16;
        const auto limit = static_cast<unsigned>(std::clamp(requested, 1.0, 64.0));
        auto results = pool ? pool->poll(limit) : std::vector<gincy::Result>{};
        LUA->CreateTable();
        unsigned index = 0;
        for (const auto& result : results) {
            LUA->PushNumber(++index);
            LUA->CreateTable();
            LUA->PushNumber(static_cast<double>(result.id)); LUA->SetField(-2, "id");
            LUA->PushBool(result.ok); LUA->SetField(-2, "ok");
            LUA->PushString(result.code.c_str()); LUA->SetField(-2, "code");
            LUA->PushNumber(result.milliseconds); LUA->SetField(-2, "milliseconds");
            LUA->CreateTable();
            unsigned rowIndex = 0;
            for (const auto& row : result.rows) {
                LUA->PushNumber(++rowIndex);
                LUA->CreateTable();
                for (const auto& column : row) {
                    if (column.second) LUA->PushString(column.second->c_str(), static_cast<unsigned>(column.second->size()));
                    else LUA->PushNil();
                    LUA->SetField(-2, column.first.c_str());
                }
                LUA->SetTable(-3);
            }
            LUA->SetField(-2, "rows");
            LUA->SetTable(-3);
        }
        return 1;
    } catch (...) { return fail(LUA, "native poll failed"); }
}
LUA_FUNCTION(stats) {
    LUA->CreateTable();
    LUA->PushNumber(pool ? static_cast<double>(pool->pending()) : 0);
    LUA->SetField(-2, "pending");
    LUA->PushBool(pool != nullptr); LUA->SetField(-2, "configured");
    return 1;
}
LUA_FUNCTION(shutdown) {
    static_cast<void>(LUA);
    gincy::httpStop();
    pool.reset();
    return 0;
}
LUA_FUNCTION(version_info) {
    LUA->CreateTable();
    LUA->PushString(gincy::kProduct); LUA->SetField(-2, "product");
    LUA->PushString(gincy::kFrameworkAPI); LUA->SetField(-2, "framework_api");
    LUA->PushNumber(gincy::kNativeABI); LUA->SetField(-2, "native_abi");
    LUA->PushNumber(gincy::kPackageFormat); LUA->SetField(-2, "package_format");
    LUA->PushNumber(gincy::kNetworkProtocol); LUA->SetField(-2, "network_protocol");
    LUA->PushNumber(gincy::kManagementAPI); LUA->SetField(-2, "management_api");
    LUA->PushNumber(gincy::kContentSchemaAPI); LUA->SetField(-2, "content_schema_api");
    LUA->PushString(gincy::kChannel); LUA->SetField(-2, "channel");
#ifdef _WIN32
    LUA->PushString("windows");
#else
    LUA->PushString("linux");
#endif
    LUA->SetField(-2, "platform");
    LUA->PushString(sizeof(void*) == 8 ? "x64" : "x86"); LUA->SetField(-2, "architecture");
    LUA->PushString("release"); LUA->SetField(-2, "build");
    return 1;
}
LUA_FUNCTION(ensure_layout) {
    std::vector<std::string> paths;
    if (!LUA->IsType(1, GarrysMod::Lua::Type::Table)) return fail(LUA, "expected directory list");
    for (unsigned i = 1; i <= 64; ++i) {
        LUA->PushNumber(i);
        LUA->GetTable(1);
        if (LUA->IsType(-1, GarrysMod::Lua::Type::Nil)) { LUA->Pop(); break; }
        try { paths.push_back(readString(LUA, -1, 512)); } catch (const std::exception& error) { LUA->Pop(); return fail(LUA, error.what()); }
        LUA->Pop();
    }
    std::string error;
    if (!gincy::ensureDirectories(paths, error)) return fail(LUA, error.c_str());
    LUA->PushBool(true);
    return 1;
}
LUA_FUNCTION(random_token) {
    try {
        auto token = gincy::randomToken(32);
        LUA->PushString(token.c_str());
        return 1;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
}
LUA_FUNCTION(http_start) {
    if (!LUA->IsType(1, GarrysMod::Lua::Type::Table)) return fail(LUA, "expected http config");
    try {
        gincy::HttpConfig config;
        config.product = gincy::kProduct;
        config.managementApi = gincy::kManagementAPI;
        LUA->GetField(1, "host"); if (!LUA->IsType(-1, GarrysMod::Lua::Type::Nil)) config.host = readString(LUA, -1, 128); LUA->Pop();
        LUA->GetField(1, "port"); if (LUA->IsType(-1, GarrysMod::Lua::Type::Number)) config.port = static_cast<unsigned>(LUA->GetNumber(-1)); LUA->Pop();
        LUA->GetField(1, "token"); config.token = readString(LUA, -1, 256); LUA->Pop();
        LUA->GetField(1, "web_root"); if (!LUA->IsType(-1, GarrysMod::Lua::Type::Nil)) config.webRoot = readString(LUA, -1, 512); LUA->Pop();
        LUA->GetField(1, "product"); if (!LUA->IsType(-1, GarrysMod::Lua::Type::Nil)) config.product = readString(LUA, -1, 32); LUA->Pop();
        LUA->GetField(1, "management_api"); if (LUA->IsType(-1, GarrysMod::Lua::Type::Number)) config.managementApi = static_cast<unsigned>(LUA->GetNumber(-1)); LUA->Pop();
        std::string error;
        if (!gincy::httpStart(config, error)) return fail(LUA, error.c_str());
        LUA->PushNumber(gincy::httpPort());
        return 1;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
}
LUA_FUNCTION(http_poll) {
    auto requests = gincy::httpPoll(8);
    LUA->CreateTable();
    unsigned index = 0;
    for (const auto& request : requests) {
        LUA->PushNumber(++index);
        LUA->CreateTable();
        LUA->PushNumber(static_cast<double>(request.id)); LUA->SetField(-2, "id");
        LUA->PushString(request.method.c_str()); LUA->SetField(-2, "method");
        LUA->PushString(request.path.c_str()); LUA->SetField(-2, "path");
        LUA->PushString(request.body.c_str(), static_cast<unsigned>(request.body.size())); LUA->SetField(-2, "body");
        LUA->PushString(request.host.c_str()); LUA->SetField(-2, "host");
        LUA->SetTable(-3);
    }
    return 1;
}
LUA_FUNCTION(http_reply) {
    if (!LUA->IsType(1, GarrysMod::Lua::Type::Number)) return fail(LUA, "expected request id");
    auto id = static_cast<std::uint64_t>(LUA->GetNumber(1));
    int status = LUA->IsType(2, GarrysMod::Lua::Type::Number) ? static_cast<int>(LUA->GetNumber(2)) : 500;
    std::string type = "application/json", body;
    try {
        if (!LUA->IsType(3, GarrysMod::Lua::Type::Nil)) type = readString(LUA, 3, 128);
        if (LUA->IsType(4, GarrysMod::Lua::Type::String)) {
            unsigned size = 0;
            const char* bytes = LUA->GetString(4, &size);
            body.assign(bytes, size);
        }
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
    gincy::httpReply(id, status, type, body);
    LUA->PushBool(true);
    return 1;
}
LUA_FUNCTION(write_text) {
    try {
        auto relative = readString(LUA, 1, 240);
        if (relative.find("..") != std::string::npos || relative.find('\\') != std::string::npos) return fail(LUA, "invalid path");
        bool allowed = relative.rfind("gincy_dev/modules/", 0) == 0 || relative.rfind("gincy_dev/content/", 0) == 0 || relative.rfind("gincy_content/", 0) == 0;
        if (!allowed) return fail(LUA, "path is outside the Gincy content trees");
        if (!LUA->IsType(2, GarrysMod::Lua::Type::String)) return fail(LUA, "expected text");
        unsigned size = 0;
        const char* bytes = LUA->GetString(2, &size);
        if (size > 1024 * 1024) return fail(LUA, "file too large");
        auto slash = relative.rfind('/');
        if (slash != std::string::npos) {
            std::string error;
            if (!gincy::ensureDirectories({"garrysmod/" + relative.substr(0, slash)}, error)) return fail(LUA, error.c_str());
        }
        std::ofstream output(std::string("garrysmod/") + relative, std::ios::binary);
        if (!output) return fail(LUA, "write failed");
        output.write(bytes, static_cast<std::streamsize>(size));
        LUA->PushBool(true);
        return 1;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
}
LUA_FUNCTION(write_module_file) {
    try {
        auto relative = readString(LUA, 1, 240);
        if (relative.find("..") != std::string::npos || relative.find('\\') != std::string::npos || relative.rfind("gincy_modules/", 0) != 0 || relative.size() < 6 || relative.substr(relative.size() - 5) != ".gmod")
            return fail(LUA, "invalid package path");
        if (!LUA->IsType(2, GarrysMod::Lua::Type::String)) return fail(LUA, "expected bytes");
        unsigned size = 0;
        const char* bytes = LUA->GetString(2, &size);
        if (size > 16 * 1024 * 1024) return fail(LUA, "package too large");
        std::string error;
        if (!gincy::ensureDirectories({"garrysmod/gincy_modules"}, error)) return fail(LUA, error.c_str());
        std::ofstream output(std::string("garrysmod/") + relative, std::ios::binary);
        if (!output) return fail(LUA, "write failed");
        output.write(bytes, static_cast<std::streamsize>(size));
        LUA->PushBool(true);
        return 1;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
}
LUA_FUNCTION(http_stop) {
    static_cast<void>(LUA);
    gincy::httpStop();
    LUA->PushBool(true);
    return 1;
}
LUA_FUNCTION(compile_client) {
    try {
        auto source = readString(LUA, 1, 1024 * 1024);
        auto name = LUA->IsType(2, GarrysMod::Lua::Type::String) ? readString(LUA, 2, 240) : std::string("client.lua");
        bool maximum = LUA->IsType(3, GarrysMod::Lua::Type::Bool) && LUA->GetBool(3);
        bool fallback = LUA->IsType(4, GarrysMod::Lua::Type::Bool) && LUA->GetBool(4);
        auto result = gincy::compileClientLua(source, name, maximum, fallback);
        if (result.usedFallback) { LUA->PushBool(false); LUA->PushString("fallback"); return 2; }
        LUA->PushString(result.bytecode.c_str(), static_cast<unsigned>(result.bytecode.size()));
        LUA->PushNumber(result.instructions);
        return 2;
    } catch (const gincy::GvmError& error) {
        LUA->PushNil();
        LUA->PushString(error.what());
        return 2;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
}
LUA_FUNCTION(generate_identity) {
    try {
        auto pem = gincy::generateRsaPrivatePem();
        auto pub = gincy::rsaPublicPem(pem);
        LUA->PushString(pem.c_str(), static_cast<unsigned>(pem.size()));
        LUA->PushString(pub.c_str(), static_cast<unsigned>(pub.size()));
        OPENSSL_cleanse(&pem[0], pem.size());
        return 2;
    } catch (const std::exception& error) { return fail(LUA, error.what()); }
}
LUA_FUNCTION(http_running) {
    LUA->PushBool(gincy::httpRunning());
    return 1;
}
}
GMOD_MODULE_OPEN() {
    LUA->CreateTable();
    for (const auto& entry : std::initializer_list<std::pair<const char*, GarrysMod::Lua::CFunc>>{
        {"load_entry", load_entry}, {"verify_package", verify_package}, {"configure", configure}, {"submit", submit}, {"poll", poll}, {"stats", stats}, {"shutdown", shutdown},
        {"version", version_info}, {"ensure_layout", ensure_layout}, {"random_token", random_token},
        {"http_start", http_start}, {"http_poll", http_poll}, {"http_reply", http_reply}, {"http_stop", http_stop}, {"http_running", http_running}, {"write_module_file", write_module_file},
        {"compile_client", compile_client}, {"generate_identity", generate_identity}, {"write_text", write_text}}) {
        LUA->PushCFunction(entry.second);
        LUA->SetField(-2, entry.first);
    }
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->Push(-2);
    LUA->SetField(-2, "gincy_native");
    LUA->Pop();
    return 1;
}
GMOD_MODULE_CLOSE() {
    static_cast<void>(LUA);
    gincy::httpStop();
    pool.reset();
    return 0;
}
