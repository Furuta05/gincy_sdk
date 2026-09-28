#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace gincy {
struct PackageFile { std::string path; std::uint8_t realm; std::string data; bool protectedPayload = false; std::uint8_t protectionKind = 0; };
struct Package {
    std::string manifest;
    std::string signer;
    std::string signerRaw;
    std::string digest;
    std::vector<PackageFile> files;
    int formatVersion = 0;
    int flags = 0;
};
struct PackedFile { std::string path; std::uint8_t realm; std::string data; };
std::string decryptEntry(const PackageFile& entry, const std::string& privateKey, const std::string& signerDigest = {});
Package verifyPackage(const std::string& bytes, const std::string& publicKey);
Package inspectPackage(const std::string& bytes);
std::string packPackage(std::string manifestJson, const std::vector<PackedFile>& files, const std::string& privateKeyPem, const std::string& recipientPem);
struct PackRequest {
    std::string manifestJson;
    std::vector<PackedFile> files;
    std::string privateKeyPem;
    std::string recipientPem;
    std::string serverProtection = "open";
    std::string clientProtection = "open";
    bool drm = false;
    std::string fingerprint = "false";
    bool allowFallback = false;
    bool allowUnpack = false;
    std::string report;
};
std::string packPackage(PackRequest& request);
std::string generateEd25519PrivatePem();
std::string ed25519PublicPem(const std::string& privatePem);
std::string generateRsaPrivatePem();
std::string rsaPublicPem(const std::string& privatePem);
std::string randomToken(unsigned bytes = 32);
bool ensureDirectories(const std::vector<std::string>& paths, std::string& error);
}
