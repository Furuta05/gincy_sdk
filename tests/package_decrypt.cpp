#include "package.hpp"
#include <openssl/evp.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <array>
#include <stdexcept>
std::string read(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("file missing");
    return std::string(std::istreambuf_iterator<char>(stream), {});
}
int main(int argc, char** argv) {
    if (argc != 5) return 2;
    try {
        const auto package = gincy::verifyPackage(read(argv[1]), read(argv[2]));
        const auto privateKey = read(argv[3]);
        bool found = false;
        for (auto entry : package.files) {
            if (!entry.protectedPayload) continue;
            const auto source = gincy::decryptEntry(entry, privateKey);
            std::array<unsigned char, 32> bytes{};
            unsigned length = 0;
            if (EVP_Digest(source.data(), source.size(), bytes.data(), &length, EVP_sha256(), nullptr) != 1) return 1;
            std::string hash;
            for (const auto byte : bytes) { hash += "0123456789abcdef"[byte >> 4]; hash += "0123456789abcdef"[byte & 15]; }
            if (hash != argv[4]) throw std::runtime_error("plaintext mismatch");
            entry.data.back() ^= 1;
            bool rejected = false;
            try { gincy::decryptEntry(entry, privateKey); } catch (...) { rejected = true; }
            if (!rejected) throw std::runtime_error("tampered GCM tag accepted");
            found = true;
        }
        if (!found) return 1;
        std::cout << "PASS native envelope and tag validation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
