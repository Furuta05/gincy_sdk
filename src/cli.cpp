#include "package.hpp"
#include "json.hpp"
#include "gvm.hpp"
#include "gincy_version.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
namespace fs = std::filesystem;
namespace {
std::string readAll(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), {});
}
void writeAll(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write " + path.string());
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
}
struct Lexer {
    std::string text;
    std::size_t at = 0;
    std::string token;
    static bool ident(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; }
    void skip() {
        while (at < text.size()) {
            if (std::isspace(static_cast<unsigned char>(text[at]))) { ++at; continue; }
            if (text.compare(at, 2, "--") == 0) throw std::runtime_error("Manifest comments are forbidden");
            break;
        }
    }
    std::string next() {
        skip();
        if (at >= text.size()) return token = {};
        char ch = text[at];
        if (ch == '"' || ch == '\'') {
            char quote = ch; ++at;
            std::string out;
            while (at < text.size() && text[at] != quote) {
                if (text[at] == '\\') { ++at; if (at < text.size()) out.push_back(text[at++]); }
                else out.push_back(text[at++]);
            }
            if (at >= text.size()) throw std::runtime_error("unterminated string");
            ++at;
            return token = "\"" + out;
        }
        if (std::string("{}=,;").find(ch) != std::string::npos) { ++at; return token = std::string(1, ch); }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            std::size_t start = at++;
            while (at < text.size() && (std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '.')) ++at;
            return token = text.substr(start, at - start);
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            std::size_t start = at++;
            while (at < text.size() && ident(text[at])) ++at;
            return token = text.substr(start, at - start);
        }
        throw std::runtime_error("invalid manifest token");
    }
};
gincy::Json parseLua(Lexer& lexer);
gincy::Json parseLuaValue(Lexer& lexer) {
    if (lexer.token == "{") {
        auto object = gincy::Json::object();
        auto array = gincy::Json::array();
        bool isObject = false, isArray = false;
        lexer.next();
        while (lexer.token != "}") {
            if (lexer.token.empty()) throw std::runtime_error("unterminated table");
            std::string key = lexer.token;
            std::string look = lexer.next();
            if (look == "=") {
                isObject = true;
                if (key.size() && key[0] == '"') key.erase(key.begin());
                lexer.next();
                object.set(key, parseLuaValue(lexer));
            } else {
                isArray = true;
                lexer.token = key;
                array.push(parseLuaValue(lexer));
            }
            if (lexer.token == "," || lexer.token == ";") lexer.next();
            else if (lexer.token != "}") throw std::runtime_error("expected table separator");
        }
        lexer.next();
        if (isObject && isArray) throw std::runtime_error("Mixed tables are forbidden");
        return isArray ? array : object;
    }
    if (lexer.token == "true" || lexer.token == "false") {
        auto value = gincy::Json::boolean(lexer.token == "true");
        lexer.next();
        return value;
    }
    if (!lexer.token.empty() && lexer.token[0] == '"') {
        auto value = gincy::Json::string(lexer.token.substr(1));
        lexer.next();
        return value;
    }
    if (!lexer.token.empty() && (lexer.token[0] == '-' || std::isdigit(static_cast<unsigned char>(lexer.token[0])))) {
        auto value = gincy::Json::number(std::stod(lexer.token));
        lexer.next();
        return value;
    }
    throw std::runtime_error("Invalid literal: " + lexer.token);
}
gincy::Json parseLua(Lexer& lexer) {
    if (lexer.next() != "return") throw std::runtime_error("Manifest must be a literal Lua return table");
    lexer.next();
    auto value = parseLuaValue(lexer);
    if (!lexer.token.empty()) throw std::runtime_error("Trailing manifest tokens");
    return value;
}
fs::path findManifest(const fs::path& root) {
    if (fs::exists(root / "manifest.lua")) return root / "manifest.lua";
    fs::path match;
    if (fs::exists(root / "lua" / "gincy")) {
        for (const auto& entry : fs::directory_iterator(root / "lua" / "gincy")) {
            auto candidate = entry.path() / "manifest.lua";
            if (fs::is_regular_file(candidate)) {
                if (!match.empty()) throw std::runtime_error("Expected one manifest.lua");
                match = candidate;
            }
        }
    }
    if (match.empty()) throw std::runtime_error("Expected one manifest.lua at root or lua/gincy/ID/");
    return match;
}
std::uint8_t realmOf(const std::string& relative, const std::string& filename) {
    if (relative.rfind("migrations/", 0) == 0 && filename.size() >= 4 && filename.substr(filename.size() - 4) == ".sql") return 4;
    if (filename.size() >= 7 && filename.substr(filename.size() - 4) == ".lua") {
        auto prefix = filename.substr(0, 3);
        if (prefix == "sv_") return 1;
        if (prefix == "sh_") return 2;
        if (prefix == "cl_") return 3;
    }
    return 0;
}
std::vector<gincy::PackedFile> collect(const fs::path& root) {
    std::vector<gincy::PackedFile> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() == "manifest.lua") continue;
        auto relative = entry.path().lexically_relative(root).generic_string();
        auto realm = realmOf(relative, entry.path().filename().string());
        if (!realm) throw std::runtime_error("Unclassified payload: " + relative);
        files.push_back({relative, realm, readAll(entry.path())});
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    return files;
}
std::string flag(int argc, char** argv, const std::string& name, const std::string& fallback = {}) {
    for (int i = 0; i < argc - 1; ++i) if (argv[i] == name) return argv[i + 1];
    return fallback;
}
bool has(int argc, char** argv, const std::string& name) {
    for (int i = 0; i < argc; ++i) if (argv[i] == name) return true;
    return false;
}
std::string field(const gincy::Json& object, const std::string& key, const std::string& fallback = "-") {
    if (const auto* value = object.find(key)) {
        if (value->isString()) return value->asString();
        if (value->isNumber()) {
            if (value->isInteger()) return std::to_string(value->asInteger());
            return std::to_string(value->asNumber());
        }
        if (value->isBool()) return value->asBool() ? "true" : "false";
        if (value->isObject() && key == "drm") {
            if (const auto* enabled = value->find("enabled")) if (enabled->isBool()) return enabled->asBool() ? "ON" : "OFF";
        }
    }
    return fallback;
}
void printInspect(const gincy::Package& package, bool json) {
    auto manifest = gincy::Json::parse(package.manifest);
    if (json) {
        std::cout << package.manifest << "\n";
        return;
    }
    int server = 0, client = 0, gvm = 0;
    for (const auto& entry : package.files) {
        if (entry.realm == 1 && entry.protectedPayload) ++server;
        if ((entry.realm == 2 || entry.realm == 3) && entry.protectedPayload) ++client;
        if (entry.protectionKind == 3 || entry.protectionKind == 4) ++gvm;
    }
    std::string protection = field(manifest, "protection", "OPEN");
    if (const auto* policy = manifest.find("protection_policy")) {
        if (policy->isObject()) {
            auto serverMode = field(*policy, "server", "open");
            auto clientMode = field(*policy, "client", "open");
            if (serverMode == "maximum" || clientMode == "maximum") protection = "MAXIMUM";
            else if (serverMode == "protected" || clientMode == "protected") protection = "PROTECTED";
        }
    }
    std::cout << "Package       " << field(manifest, "name", field(manifest, "id")) << "\n"
              << "Version       " << field(manifest, "version") << "\n"
              << "Format        " << package.formatVersion << "\n"
              << "Built with    " << field(manifest, "built_with_gincy") << "\n"
              << "API           " << field(manifest, "required_framework_api") << "\n"
              << "Signature     " << (package.digest.empty() ? "-" : "PRESENT") << "\n"
              << "Protection    " << protection << "\n"
              << "DRM           " << field(manifest, "drm", "OFF") << "\n"
              << "Fingerprint   " << (manifest.find("fingerprint") ? "PRESENT" : "OFF") << "\n"
              << "Unpack        " << field(manifest, "allow_unpack", "false") << "\n"
              << "Server        " << server << " protected entries\n"
              << "Client        " << gvm << " GVM programs / " << client << " protected entries\n"
              << "Digest        " << package.digest << "\n";
}
int usage() {
    std::cerr << "gincy " << gincy::kProduct << "\n"
              << "  gincy version\n"
              << "  gincy doctor\n"
              << "  gincy errors explain <code>\n"
              << "  gincy package build <dir> --key signer.pem --output out.gmod\n"
              << "      [--protected] [--maximum] [--drm] [--fingerprint build|recipient]\n"
              << "      [--recipient server.pub.pem] [--allow-fallback] [--unpack]\n"
              << "  gincy package inspect <package> [--public-key trust.pem] [--json]\n"
              << "  gincy package verify <package> --public-key trust.pem\n"
              << "  gincy package migrate <package> --key signer.pem --output out.gmod\n"
              << "  gincy package unpack <package> --output <dir> [--public-key trust.pem]\n"
              << "  gincy pack|sign|inspect|verify   (aliases)\n"
              << "  gincy keygen <private.pem>\n"
              << "  gincy identity <private.pem>\n";
    return 2;
}
const char* explainCode(const std::string& code) {
    if (code == "GINCY-PKG-SIGNATURE-INVALID") return "The package signature does not match a trusted publisher key.";
    if (code == "GINCY-PKG-FORMAT-UNSUPPORTED") return "This .gmod format is not supported. Rebuild with Gincy 3.2 package tooling.";
    if (code == "GINCY-PKG-COMPATIBILITY") return "Package Gincy/API/ABI constraints do not match this runtime.";
    if (code == "GINCY-PKG-RECIPIENT-MISMATCH") return "This package is bound to a different server identity.";
    if (code == "GINCY-PKG-INTEGRITY") return "An entry hash failed; the package was truncated or modified.";
    if (code == "GINCY-PKG-ENTITLEMENT") return "DRM entitlement is missing, expired, or not signed for this identity.";
    if (code == "GINCY-PKG-UNPACK-DENIED") return "unpack=false is signed policy. Official tooling will not export protected source.";
    if (code == "GINCY-GVM-COMPILE") return "Client GVM compilation failed. See file, line and construct in the report.";
    if (code == "GINCY-GVM-UNSUPPORTED") return "This GLua construct cannot be compiled to GVM in maximum mode.";
    if (code == "GINCY-GVM-PROGRAM-INVALID") return "Client program bytecode is malformed or failed authentication.";
    if (code == "GINCY-NATIVE-ABI-MISMATCH") return "Replace gmsv_gincy_core with the matching Gincy native build.";
    return nullptr;
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2) return usage();
        std::string command = argv[1];
        int pathIndex = 2;
        if (command == "package" && argc >= 3) {
            command = argv[2];
            pathIndex = 3;
            if (command == "build") command = "pack";
        }
        if (command == "version" || command == "--version") {
            std::cout << "Gincy Product        " << gincy::kProduct << "\n"
                      << "Framework API        " << gincy::kFrameworkAPI << "\n"
                      << "Native ABI           " << gincy::kNativeABI << "\n"
                      << "Package Format       " << gincy::kPackageFormat << "\n"
                      << "Network Protocol     " << gincy::kNetworkProtocol << "\n"
                      << "Management API       " << gincy::kManagementAPI << "\n"
                      << "Content Schema API   " << gincy::kContentSchemaAPI << "\n"
                      << "\n"
                      << "Native Runtime       compatible\n"
                      << "WebUI                 compatible\n";
            return 0;
        }
        if (command == "doctor") {
            auto token = gincy::randomToken(16);
            auto compiled = gincy::compileClientLua("print(1+2)\n", "doctor.lua", false, false);
            std::cout << "Gincy Doctor\n----------------------------------------\n\n"
                      << "[OK] Core            " << gincy::kProduct << "\n"
                      << "[OK] Crypto          ready\n"
                      << "[OK] Packages        format " << gincy::kPackageFormat << "\n"
                      << "[OK] Client VM       " << (gincy::isGvmBytecode(compiled.bytecode) ? "ready" : "FAIL") << "\n"
                      << "[OK] Token           " << (token.size() == 32 ? "ready" : "FAIL") << "\n"
                      << "\nResult: HEALTHY\n";
            return 0;
        }
        if (command == "errors" && argc >= 4 && std::string(argv[2]) == "explain") {
            auto text = explainCode(argv[3]);
            if (!text) { std::cerr << "Unknown error code\n"; return 1; }
            std::cout << argv[3] << "\n" << text << "\n";
            return 0;
        }
        if (command == "keygen") {
            if (argc < 3) return usage();
            auto pem = gincy::generateEd25519PrivatePem();
            writeAll(argv[2], pem);
            auto publicPath = fs::path(argv[2]);
            writeAll(publicPath.parent_path() / (publicPath.stem().string() + ".pub.pem"), gincy::ed25519PublicPem(pem));
            std::cout << "wrote " << argv[2] << "\n";
            return 0;
        }
        if (command == "identity") {
            if (argc < 3) return usage();
            auto pem = gincy::generateRsaPrivatePem();
            writeAll(argv[2], pem);
            auto publicPath = fs::path(argv[2]);
            publicPath.replace_extension();
            writeAll(publicPath.string() + ".pub.pem", gincy::rsaPublicPem(pem));
            std::cout << "wrote " << argv[2] << "\n";
            return 0;
        }
        if (command == "pack" || command == "sign") {
            if (argc <= pathIndex) return usage();
            auto keyPath = flag(argc, argv, "--key");
            auto output = flag(argc, argv, "--output");
            auto recipient = flag(argc, argv, "--recipient");
            if (keyPath.empty() || output.empty()) return usage();
            auto manifestPath = findManifest(argv[pathIndex]);
            Lexer lexer{readAll(manifestPath)};
            auto manifest = parseLua(lexer);
            auto files = collect(manifestPath.parent_path());
            gincy::PackRequest request;
            request.manifestJson = manifest.dump();
            request.files = files;
            request.privateKeyPem = readAll(keyPath);
            request.recipientPem = recipient.empty() ? std::string() : readAll(recipient);
            request.drm = has(argc, argv, "--drm");
            request.allowFallback = has(argc, argv, "--allow-fallback");
            request.allowUnpack = has(argc, argv, "--unpack");
            auto fingerprint = flag(argc, argv, "--fingerprint", "false");
            request.fingerprint = fingerprint.empty() ? "false" : fingerprint;
            if (has(argc, argv, "--maximum")) {
                request.serverProtection = "maximum";
                request.clientProtection = "maximum";
            } else if (has(argc, argv, "--protected")) {
                request.serverProtection = "protected";
                request.clientProtection = "protected";
            }
            auto bytes = gincy::packPackage(request);
            writeAll(output, bytes);
            std::cout << output << " " << bytes.size() << "\n" << request.report;
            return 0;
        }
        if (command == "verify") {
            if (argc <= pathIndex) return usage();
            auto key = flag(argc, argv, "--public-key");
            if (key.empty()) return usage();
            auto package = gincy::verifyPackage(readAll(argv[pathIndex]), readAll(key));
            std::cout << "Signature     VALID\nFormat        " << package.formatVersion << "\nFiles         " << package.files.size() << "\nDigest        " << package.digest << "\n";
            return 0;
        }
        if (command == "inspect") {
            if (argc <= pathIndex) return usage();
            auto bytes = readAll(argv[pathIndex]);
            auto key = flag(argc, argv, "--public-key");
            gincy::Package package;
            if (!key.empty()) package = gincy::verifyPackage(bytes, readAll(key));
            else package = gincy::inspectPackage(bytes);
            printInspect(package, has(argc, argv, "--json"));
            return 0;
        }
        if (command == "migrate") {
            if (argc <= pathIndex) return usage();
            auto keyPath = flag(argc, argv, "--key");
            auto output = flag(argc, argv, "--output");
            if (keyPath.empty() || output.empty()) return usage();
            auto package = gincy::inspectPackage(readAll(argv[pathIndex]));
            if (package.formatVersion >= 2) {
                std::cerr << "GINCY-PKG-FORMAT-UNSUPPORTED already format " << package.formatVersion << "\n";
                return 1;
            }
            for (const auto& entry : package.files) {
                if (entry.protectedPayload) {
                    std::cerr << "GINCY-PKG-FORMAT-UNSUPPORTED protected v1 cannot inherit V2 guarantees; rebuild from source\n";
                    return 1;
                }
            }
            gincy::PackRequest request;
            request.manifestJson = package.manifest;
            request.privateKeyPem = readAll(keyPath);
            for (const auto& entry : package.files) request.files.push_back({entry.path, entry.realm, entry.data});
            auto bytes = gincy::packPackage(request);
            writeAll(output, bytes);
            std::cout << "Migrated container to package format 2 (open). Rebuild from source for protection.\n" << output << " " << bytes.size() << "\n";
            return 0;
        }
        if (command == "unpack") {
            if (argc <= pathIndex) return usage();
            auto output = flag(argc, argv, "--output");
            if (output.empty()) return usage();
            auto key = flag(argc, argv, "--public-key");
            auto bytes = readAll(argv[pathIndex]);
            gincy::Package package = key.empty() ? gincy::inspectPackage(bytes) : gincy::verifyPackage(bytes, readAll(key));
            auto manifest = gincy::Json::parse(package.manifest);
            bool allowed = false;
            if (const auto* value = manifest.find("allow_unpack")) allowed = value->isBool() && value->asBool();
            if (const auto* policy = manifest.find("protection_policy")) {
                if (policy->isObject()) if (const auto* unpack = policy->find("unpack")) allowed = unpack->isBool() && unpack->asBool();
            }
            if (!allowed) {
                std::cerr << "GINCY-PKG-UNPACK-DENIED\nOfficial tooling will not export this package.\n";
                return 1;
            }
            auto root = fs::path(output);
            writeAll(root / "manifest.json", package.manifest);
            for (const auto& entry : package.files) {
                if (entry.protectedPayload) {
                    std::cerr << "GINCY-PKG-UNPACK-DENIED protected entry " << entry.path << "\n";
                    return 1;
                }
                writeAll(root / entry.path, entry.data);
            }
            std::cout << "unpacked " << package.files.size() << " entries\n";
            return 0;
        }
        return usage();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        auto text = explainCode(error.what());
        if (text) std::cerr << text << "\n";
        return 1;
    }
}
