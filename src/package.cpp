#include "package.hpp"
#include "json.hpp"
#include "gincy_version.hpp"
#include "gvm.hpp"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace gincy {
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string compatibilityRange(const char* version) {
    int major = 0, minor = 0, patch = 0;
    if (std::sscanf(version, "%d.%d.%d", &major, &minor, &patch) < 2) return std::string(">=") + version;
    std::ostringstream out;
    out << ">=" << major << "." << minor << ".0 <" << (major + 1) << ".0.0";
    return out.str();
}
std::uint64_t integer(const std::string& data, std::size_t at, unsigned size) {
    require(at <= data.size() && size <= data.size() - at, "truncated integer");
    std::uint64_t result = 0;
    for (unsigned i = 0; i < size; ++i) result |= std::uint64_t(static_cast<unsigned char>(data[at + i])) << (8 * i);
    return result;
}
std::string digest(const std::string& value) {
    std::array<unsigned char, 32> bytes{};
    unsigned size = 0;
    require(EVP_Digest(value.data(), value.size(), bytes.data(), &size, EVP_sha256(), nullptr) == 1 && size == 32, "hash failed");
    return std::string(reinterpret_cast<const char*>(bytes.data()), size);
}
std::string hex(const std::string& value) {
    std::string result;
    for (unsigned char ch : value) { result += "0123456789abcdef"[ch >> 4]; result += "0123456789abcdef"[ch & 15]; }
    return result;
}
bool safePath(const std::string& path) {
    if (path.empty() || path.size() > 240 || path.front() == '/' || path.back() == '/') return false;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const auto part = path.substr(start, i - start);
            if (part.empty() || part == "." || part == "..") return false;
            start = i + 1;
        } else {
            unsigned char ch = path[i];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
        }
    }
    return true;
}
}
Package verifyPackage(const std::string& bytes, const std::string& publicKey) {
    require(bytes.size() >= 124 && bytes.size() <= 16 * 1024 * 1024, "invalid package size");
    const auto formatVersion = integer(bytes, 8, 2);
    const auto headerFlags = integer(bytes, 10, 2);
    require(bytes.compare(0, 8, "GINCYMOD") == 0 && ((formatVersion == 1 && headerFlags == 0) || (formatVersion == 2 && headerFlags <= 1 && (headerFlags & ~1) == 0)), "GINCY-PKG-FORMAT-UNSUPPORTED");
    const auto metadataSize = integer(bytes, 12, 4), count = integer(bytes, 16, 4), payloadSize = integer(bytes, 20, 8);
    require(metadataSize > 0 && metadataSize <= 65536 && count > 0 && count <= 512, "invalid manifest or file count");
    require(payloadSize <= 16 * 1024 * 1024 && 60 + metadataSize + payloadSize + 64 == bytes.size(), "length mismatch");
    require(publicKey.size() <= 4096, "public key too large");
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(publicKey.data(), static_cast<int>(publicKey.size())), BIO_free);
    require(bio != nullptr, "key allocation failed");
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    require(key && EVP_PKEY_id(key.get()) == EVP_PKEY_ED25519, "expected Ed25519 trust key");
    std::array<unsigned char, 32> raw{};
    std::size_t rawSize = raw.size();
    require(EVP_PKEY_get_raw_public_key(key.get(), raw.data(), &rawSize) == 1 && rawSize == raw.size(), "invalid trust key");
    require(digest(std::string(reinterpret_cast<char*>(raw.data()), raw.size())) == bytes.substr(28, 32), "untrusted signer");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(context && EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1, "signature initialization failed");
    require(EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(bytes.data() + bytes.size() - 64), 64, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size() - 64) == 1, "GINCY-PKG-SIGNATURE-INVALID");
    Package result;
    result.manifest = bytes.substr(60, metadataSize);
    result.signer = hex(bytes.substr(28, 32));
    result.signerRaw = bytes.substr(28, 32);
    result.digest = hex(digest(bytes));
    result.formatVersion = static_cast<int>(formatVersion);
    result.flags = static_cast<int>(headerFlags);
    std::size_t offset = 60 + metadataSize;
    std::set<std::string> names;
    for (unsigned i = 0; i < count; ++i) {
        require(offset + 44 <= bytes.size() - 64, "truncated entry");
        const auto nameSize = integer(bytes, offset, 2), realm = integer(bytes, offset + 2, 1), reserved = integer(bytes, offset + 3, 1), size = integer(bytes, offset + 4, 8);
        const auto hash = bytes.substr(offset + 12, 32);
        offset += 44;
        require(nameSize > 0 && nameSize <= 240 && realm >= 1 && realm <= 4 && reserved <= 4 && size <= 1024 * 1024 && offset + nameSize + size <= bytes.size() - 64, "invalid payload bounds");
        if (reserved == 1) require(realm == 1 && formatVersion == 2, "invalid payload bounds");
        if (reserved == 2) require(realm == 1 && formatVersion == 2, "invalid payload bounds");
        if (reserved == 3 || reserved == 4) require((realm == 2 || realm == 3) && formatVersion == 2, "invalid payload bounds");
        PackageFile entry{bytes.substr(offset, nameSize), static_cast<std::uint8_t>(realm), bytes.substr(offset + nameSize, size)};
        entry.protectedPayload = reserved != 0;
        entry.protectionKind = static_cast<std::uint8_t>(reserved);
        offset += nameSize + size;
        require(safePath(entry.path) && names.insert(entry.path).second, "unsafe or duplicate path");
        require((entry.protectedPayload || reserved >= 3 || entry.data.find('\0') == std::string::npos) && digest(entry.data) == hash, "GINCY-PKG-INTEGRITY");
        result.files.push_back(std::move(entry));
    }
    require(offset == bytes.size() - 64, "trailing payload");
    return result;
}

std::string decryptEntry(const PackageFile& entry, const std::string& privateKey, const std::string& signerDigest) {
    require(entry.protectedPayload, "entry is not protected");
    auto aesOpen = [&](const unsigned char* key, const unsigned char* nonce, const unsigned char* cipher, int cipherSize, const unsigned char* tag) {
        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        require(ctx && EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 && EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key, nonce) == 1, "decrypt initialization failed");
        int length = 0;
        require(EVP_DecryptUpdate(ctx.get(), nullptr, &length, reinterpret_cast<const unsigned char*>(entry.path.data()), static_cast<int>(entry.path.size())) == 1, "AAD failed");
        std::vector<unsigned char> plain(cipherSize + 16);
        require(EVP_DecryptUpdate(ctx.get(), plain.data(), &length, cipher, cipherSize) == 1, "decrypt failed");
        int written = length;
        require(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(tag)) == 1, "tag setup failed");
        require(EVP_DecryptFinal_ex(ctx.get(), plain.data() + written, &length) == 1, "authentication failed");
        std::string out(reinterpret_cast<char*>(plain.data()), static_cast<std::size_t>(written + length));
        OPENSSL_cleanse(plain.data(), plain.size());
        return out;
    };
    if (entry.protectionKind == 2 || entry.protectionKind == 3) {
        require(signerDigest.size() == 32 && entry.data.size() >= 28, "invalid unbound envelope");
        auto kek = digest(std::string("GINCY-UNBOUND-V2") + signerDigest);
        const auto* data = reinterpret_cast<const unsigned char*>(entry.data.data());
        auto plain = aesOpen(reinterpret_cast<const unsigned char*>(kek.data()), data, data + 12, static_cast<int>(entry.data.size() - 28), data + entry.data.size() - 16);
        OPENSSL_cleanse(&kek[0], kek.size());
        return plain;
    }
    require(entry.realm == 1 || entry.realm == 2 || entry.realm == 3, "entry is not protected code");
    require(privateKey.size() <= 16384 && entry.data.size() >= 286, "invalid envelope");
    const auto wrappedSize = integer(entry.data, 0, 2);
    require(wrappedSize >= 256 && wrappedSize <= 1024 && wrappedSize + 30 <= entry.data.size(), "invalid wrapped key");
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(privateKey.data(), static_cast<int>(privateKey.size())), BIO_free);
    require(bio != nullptr, "key allocation failed");
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    require(key && EVP_PKEY_id(key.get()) == EVP_PKEY_RSA && EVP_PKEY_bits(key.get()) >= 2048, "invalid recipient key");
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> unwrap(EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
    require(unwrap && EVP_PKEY_decrypt_init(unwrap.get()) > 0 && EVP_PKEY_CTX_set_rsa_padding(unwrap.get(), RSA_PKCS1_OAEP_PADDING) > 0 && EVP_PKEY_CTX_set_rsa_oaep_md(unwrap.get(), EVP_sha256()) > 0 && EVP_PKEY_CTX_set_rsa_mgf1_md(unwrap.get(), EVP_sha256()) > 0, "unwrap initialization failed");
    struct Secret {
        std::vector<unsigned char> bytes;
        ~Secret() { if (!bytes.empty()) OPENSSL_cleanse(bytes.data(), bytes.size()); }
    };
    Secret secret;
    std::size_t secretSize = 0;
    const auto* data = reinterpret_cast<const unsigned char*>(entry.data.data());
    require(EVP_PKEY_decrypt(unwrap.get(), nullptr, &secretSize, data + 2, wrappedSize) > 0, "GINCY-PKG-RECIPIENT-MISMATCH");
    secret.bytes.resize(secretSize);
    require(EVP_PKEY_decrypt(unwrap.get(), secret.bytes.data(), &secretSize, data + 2, wrappedSize) > 0 && secretSize == 32, "GINCY-PKG-RECIPIENT-MISMATCH");
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> cipher(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(cipher && EVP_DecryptInit_ex(cipher.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 && EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 && EVP_DecryptInit_ex(cipher.get(), nullptr, nullptr, secret.bytes.data(), data + 2 + wrappedSize) == 1, "decrypt initialization failed");
    const auto ciphertextSize = entry.data.size() - wrappedSize - 30;
    int length = 0;
    require(EVP_DecryptUpdate(cipher.get(), nullptr, &length, reinterpret_cast<const unsigned char*>(entry.path.data()), static_cast<int>(entry.path.size())) == 1, "AAD failed");
    Secret plaintext;
    plaintext.bytes.resize(ciphertextSize + 16);
    require(EVP_DecryptUpdate(cipher.get(), plaintext.bytes.data(), &length, data + wrappedSize + 14, static_cast<int>(ciphertextSize)) == 1, "decrypt failed");
    const auto written = length;
    require(EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(data + entry.data.size() - 16)) == 1, "tag setup failed");
    require(EVP_DecryptFinal_ex(cipher.get(), plaintext.bytes.data() + written, &length) == 1, "authentication failed");
    return std::string(reinterpret_cast<const char*>(plaintext.bytes.data()), static_cast<std::size_t>(written + length));
}

namespace {
std::string pemFromKey(EVP_PKEY* key, bool privateKey) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
    require(bio != nullptr, "key allocation failed");
    if (privateKey) require(PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr) == 1, "private key encode failed");
    else require(PEM_write_bio_PUBKEY(bio.get(), key) == 1, "public key encode failed");
    char* data = nullptr;
    long size = BIO_get_mem_data(bio.get(), &data);
    require(size > 0 && data, "empty key");
    return std::string(data, static_cast<std::size_t>(size));
}
std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> loadPrivate(const std::string& pem) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    require(bio != nullptr, "key allocation failed");
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    require(key != nullptr, "invalid private key");
    return key;
}
void putInteger(std::string& out, std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
        out.push_back(static_cast<char>(value & 0xff));
        value >>= 8;
    }
}
std::string encryptEntry(const std::string& path, const std::string& data, EVP_PKEY* recipient) {
    unsigned char secret[32];
    require(RAND_bytes(secret, sizeof(secret)) == 1, "random failed");
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> wrap(EVP_PKEY_CTX_new(recipient, nullptr), EVP_PKEY_CTX_free);
    require(wrap && EVP_PKEY_encrypt_init(wrap.get()) > 0 && EVP_PKEY_CTX_set_rsa_padding(wrap.get(), RSA_PKCS1_OAEP_PADDING) > 0 && EVP_PKEY_CTX_set_rsa_oaep_md(wrap.get(), EVP_sha256()) > 0 && EVP_PKEY_CTX_set_rsa_mgf1_md(wrap.get(), EVP_sha256()) > 0, "wrap initialization failed");
    std::size_t wrappedSize = 0;
    require(EVP_PKEY_encrypt(wrap.get(), nullptr, &wrappedSize, secret, sizeof(secret)) > 0, "wrap size failed");
    std::vector<unsigned char> wrapped(wrappedSize);
    require(EVP_PKEY_encrypt(wrap.get(), wrapped.data(), &wrappedSize, secret, sizeof(secret)) > 0, "wrap failed");
    unsigned char nonce[12];
    require(RAND_bytes(nonce, sizeof(nonce)) == 1, "nonce failed");
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> cipher(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(cipher && EVP_EncryptInit_ex(cipher.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 && EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 && EVP_EncryptInit_ex(cipher.get(), nullptr, nullptr, secret, nonce) == 1, "encrypt initialization failed");
    int length = 0;
    require(EVP_EncryptUpdate(cipher.get(), nullptr, &length, reinterpret_cast<const unsigned char*>(path.data()), static_cast<int>(path.size())) == 1, "AAD failed");
    std::vector<unsigned char> ciphertext(data.size() + 16);
    require(EVP_EncryptUpdate(cipher.get(), ciphertext.data(), &length, reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size())) == 1, "encrypt failed");
    int written = length;
    require(EVP_EncryptFinal_ex(cipher.get(), ciphertext.data() + written, &length) == 1, "encrypt final failed");
    written += length;
    unsigned char tag[16];
    require(EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_GET_TAG, 16, tag) == 1, "tag failed");
    OPENSSL_cleanse(secret, sizeof(secret));
    std::string out;
    putInteger(out, wrappedSize, 2);
    out.append(reinterpret_cast<char*>(wrapped.data()), wrappedSize);
    out.append(reinterpret_cast<char*>(nonce), sizeof(nonce));
    out.append(reinterpret_cast<char*>(ciphertext.data()), static_cast<std::size_t>(written));
    out.append(reinterpret_cast<char*>(tag), sizeof(tag));
    return out;
}
}

Package inspectPackage(const std::string& bytes) {
    require(bytes.size() >= 124 && bytes.size() <= 16 * 1024 * 1024, "invalid package size");
    require(bytes.compare(0, 8, "GINCYMOD") == 0, "GINCY-PKG-FORMAT-UNSUPPORTED");
    const auto formatVersion = integer(bytes, 8, 2);
    const auto headerFlags = integer(bytes, 10, 2);
    const auto metadataSize = integer(bytes, 12, 4), count = integer(bytes, 16, 4), payloadSize = integer(bytes, 20, 8);
    require(((formatVersion == 1 && headerFlags == 0) || (formatVersion == 2 && headerFlags <= 1)), "GINCY-PKG-FORMAT-UNSUPPORTED");
    require(metadataSize > 0 && metadataSize <= 65536 && count > 0 && count <= 512, "invalid manifest or file count");
    require(payloadSize <= 16 * 1024 * 1024 && 60 + metadataSize + payloadSize + 64 == bytes.size(), "length mismatch");
    Package result;
    result.manifest = bytes.substr(60, metadataSize);
    result.signer = hex(bytes.substr(28, 32));
    result.signerRaw = bytes.substr(28, 32);
    result.digest = hex(digest(bytes));
    result.formatVersion = static_cast<int>(formatVersion);
    result.flags = static_cast<int>(headerFlags);
    std::size_t offset = 60 + metadataSize;
    for (unsigned i = 0; i < count; ++i) {
        require(offset + 44 <= bytes.size() - 64, "truncated entry");
        const auto nameSize = integer(bytes, offset, 2), realm = integer(bytes, offset + 2, 1), reserved = integer(bytes, offset + 3, 1), size = integer(bytes, offset + 4, 8);
        offset += 44;
        require(nameSize > 0 && nameSize <= 240 && size <= 1024 * 1024 && offset + nameSize + size <= bytes.size() - 64, "invalid payload bounds");
        PackageFile entry{bytes.substr(offset, nameSize), static_cast<std::uint8_t>(realm), bytes.substr(offset + nameSize, size)};
        entry.protectedPayload = reserved != 0;
        entry.protectionKind = static_cast<std::uint8_t>(reserved);
        offset += nameSize + size;
        result.files.push_back(std::move(entry));
    }
    return result;
}

std::string packPackage(std::string manifestJson, const std::vector<PackedFile>& files, const std::string& privateKeyPem, const std::string& recipientPem) {
    PackRequest request;
    request.manifestJson = std::move(manifestJson);
    request.files = files;
    request.privateKeyPem = privateKeyPem;
    request.recipientPem = recipientPem;
    if (!recipientPem.empty()) request.serverProtection = "protected";
    return packPackage(request);
}

std::string stripLuaComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size();) {
        if (src[i] == '-' && i + 1 < src.size() && src[i + 1] == '-') {
            i += 2;
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        if (src[i] == '"' || src[i] == '\'') {
            char q = src[i++];
            out.push_back(q);
            while (i < src.size() && src[i] != q) {
                if (src[i] == '\\' && i + 1 < src.size()) { out.push_back(src[i++]); out.push_back(src[i++]); }
                else out.push_back(src[i++]);
            }
            if (i < src.size()) out.push_back(src[i++]);
            continue;
        }
        out.push_back(src[i++]);
    }
    return out;
}

std::string encryptUnbound(const std::string& path, const std::string& data, const std::string& signerDigest) {
    auto kek = digest(std::string("GINCY-UNBOUND-V2") + signerDigest);
    unsigned char nonce[12];
    require(RAND_bytes(nonce, sizeof(nonce)) == 1, "nonce failed");
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> cipher(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    require(cipher && EVP_EncryptInit_ex(cipher.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 && EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 && EVP_EncryptInit_ex(cipher.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(kek.data()), nonce) == 1, "encrypt initialization failed");
    int length = 0;
    require(EVP_EncryptUpdate(cipher.get(), nullptr, &length, reinterpret_cast<const unsigned char*>(path.data()), static_cast<int>(path.size())) == 1, "AAD failed");
    std::vector<unsigned char> ciphertext(data.size() + 16);
    require(EVP_EncryptUpdate(cipher.get(), ciphertext.data(), &length, reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size())) == 1, "encrypt failed");
    int written = length;
    require(EVP_EncryptFinal_ex(cipher.get(), ciphertext.data() + written, &length) == 1, "encrypt final failed");
    written += length;
    unsigned char tag[16];
    require(EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_GET_TAG, 16, tag) == 1, "tag failed");
    std::string out;
    out.append(reinterpret_cast<char*>(nonce), 12);
    out.append(reinterpret_cast<char*>(ciphertext.data()), static_cast<std::size_t>(written));
    out.append(reinterpret_cast<char*>(tag), 16);
    return out;
}

std::string packPackage(PackRequest& request) {
    require(!request.files.empty() && request.files.size() <= 512, "Expected 1..512 payload files");
    auto key = loadPrivate(request.privateKeyPem);
    require(EVP_PKEY_id(key.get()) == EVP_PKEY_ED25519, "Signing key must be Ed25519");
    unsigned char raw[32];
    std::size_t rawSize = sizeof(raw);
    require(EVP_PKEY_get_raw_public_key(key.get(), raw, &rawSize) == 1 && rawSize == sizeof(raw), "invalid signing key");
    const auto signerDigest = digest(std::string(reinterpret_cast<char*>(raw), sizeof(raw)));
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> recipient(nullptr, EVP_PKEY_free);
    const bool recipientBound = !request.recipientPem.empty();
    if (recipientBound) {
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(request.recipientPem.data(), static_cast<int>(request.recipientPem.size())), BIO_free);
        recipient.reset(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
        require(recipient && EVP_PKEY_id(recipient.get()) == EVP_PKEY_RSA && EVP_PKEY_bits(recipient.get()) >= 2048, "Recipient must be RSA >= 2048 bits");
    }
    auto mode = [](std::string value) {
        for (char& ch : value) ch = (char)std::tolower((unsigned char)ch);
        if (value.empty()) return std::string("open");
        return value;
    };
    auto serverMode = mode(request.serverProtection);
    auto clientMode = mode(request.clientProtection);
    Json manifest = Json::parse(request.manifestJson);
    require(manifest.isObject(), "Manifest must be an object");
    if (const auto* protection = manifest.find("protection")) {
        if (protection->isObject()) {
            if (const auto* s = protection->find("server")) if (s->isString()) serverMode = mode(s->asString());
            if (const auto* s = protection->find("client")) if (s->isString()) clientMode = mode(s->asString());
            if (const auto* s = protection->find("drm")) if (s->isBool()) request.drm = s->asBool();
        } else if (protection->isString()) {
            serverMode = mode(protection->asString());
        }
    }
    const bool protectServer = serverMode == "protected" || serverMode == "maximum";
    const bool protectClient = clientMode == "protected" || clientMode == "maximum";
    const bool anyProtect = protectServer || protectClient;
    Json policy = Json::object();
    policy.set("server", Json::string(serverMode));
    policy.set("client", Json::string(clientMode));
    policy.set("drm", Json::boolean(request.drm));
    policy.set("unpack", Json::boolean(request.allowUnpack && !anyProtect));
    policy.set("fingerprint", Json::string(request.fingerprint));
    manifest.set("protection_policy", policy);
    manifest.set("allow_unpack", Json::boolean(request.allowUnpack && !anyProtect));
    manifest.set("protection", Json::string(anyProtect ? (serverMode == "maximum" || clientMode == "maximum" ? "MAXIMUM" : "PROTECTED") : "OPEN"));
    manifest.set("package_format", Json::integer(kPackageFormat));
    manifest.set("built_with_gincy", Json::string(kProduct));
    if (!manifest.find("required_gincy")) manifest.set("required_gincy", Json::string(compatibilityRange(kProduct)));
    if (!manifest.find("required_framework_api")) manifest.set("required_framework_api", Json::string(compatibilityRange(kFrameworkAPI)));
    if (request.drm) {
        Json drm = Json::object();
        drm.set("enabled", Json::boolean(true));
        drm.set("mode", Json::string("offline_lease"));
        manifest.set("drm", drm);
    }
    if (request.fingerprint == "build" || request.fingerprint == "recipient") {
        auto seed = request.fingerprint == "recipient" && recipientBound ? digest(request.recipientPem) : signerDigest;
        manifest.set("fingerprint", Json::string(hex(digest(std::string("GINCY-FP-V1") + seed + request.manifestJson))));
        manifest.set("fingerprinting", Json::string(request.fingerprint));
    }
    int serverProtected = 0, clientGvm = 0, clientFallback = 0, plaintext = 0;
    std::string payload;
    for (auto file : request.files) {
        require(safePath(file.path) && file.realm >= 1 && file.realm <= 4 && file.data.size() <= 1024 * 1024, "invalid payload");
        std::string data = file.data;
        unsigned reserved = 0;
        if (file.realm == 1 && protectServer) {
            data = stripLuaComments(data);
            if (recipientBound) { data = encryptEntry(file.path, data, recipient.get()); reserved = 1; }
            else { data = encryptUnbound(file.path, data, signerDigest); reserved = 2; }
            ++serverProtected;
        } else if ((file.realm == 2 || file.realm == 3) && protectClient) {
            try {
                auto compiled = compileClientLua(file.data, file.path, clientMode == "maximum", request.allowFallback);
                if (compiled.usedFallback) {
                    if (clientMode == "maximum") throw std::runtime_error("GINCY-GVM-UNSUPPORTED " + file.path);
                    ++clientFallback;
                    ++plaintext;
                } else {
                    data = compiled.bytecode;
                    if (recipientBound) { data = encryptEntry(file.path, data, recipient.get()); reserved = 4; }
                    else { data = encryptUnbound(file.path, data, signerDigest); reserved = 3; }
                    ++clientGvm;
                }
            } catch (const GvmError& error) {
                if (clientMode == "maximum" && !request.allowFallback) throw;
                if (!request.allowFallback) throw;
                ++clientFallback;
                ++plaintext;
            }
        } else {
            ++plaintext;
        }
        require(data.size() <= 1024 * 1024, "Protected entry exceeds limit");
        putInteger(payload, file.path.size(), 2);
        payload.push_back(static_cast<char>(file.realm));
        payload.push_back(static_cast<char>(reserved));
        putInteger(payload, data.size(), 8);
        auto hash = digest(data);
        payload.append(hash);
        payload += file.path;
        payload += data;
    }
    Json report = Json::object();
    report.set("server_protected", Json::integer(serverProtected));
    report.set("client_gvm", Json::integer(clientGvm));
    report.set("client_fallback", Json::integer(clientFallback));
    report.set("plaintext", Json::integer(plaintext));
    manifest.set("protection_report", report);
    const auto metadata = manifest.dump();
    require(!metadata.empty() && metadata.size() <= 65536, "Manifest too large");
    std::string body = "GINCYMOD";
    putInteger(body, 2, 2);
    putInteger(body, anyProtect ? 1 : 0, 2);
    putInteger(body, metadata.size(), 4);
    putInteger(body, request.files.size(), 4);
    putInteger(body, payload.size(), 8);
    body.append(signerDigest);
    body += metadata;
    body += payload;
    require(body.size() + 64 <= 16 * 1024 * 1024, "Package exceeds 16 MiB");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    unsigned char signature[64];
    std::size_t signatureSize = sizeof(signature);
    require(context && EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1 && EVP_DigestSign(context.get(), signature, &signatureSize, reinterpret_cast<const unsigned char*>(body.data()), body.size()) == 1 && signatureSize == 64, "signature failed");
    body.append(reinterpret_cast<char*>(signature), signatureSize);
    std::ostringstream text;
    text << "Gincy Protection Report\nPackage format 2\nServer: " << serverProtected << " protected entries\nClient: " << clientGvm << " GVM programs\nFallback: " << clientFallback << "\nPlaintext: " << plaintext << "\nDRM: " << (request.drm ? "ON" : "OFF") << "\nFingerprint: " << (request.fingerprint == "false" ? "OFF" : request.fingerprint) << "\n";
    request.report = text.str();
    return body;
}

std::string generateEd25519PrivatePem() {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY* raw = nullptr;
    require(ctx && EVP_PKEY_keygen_init(ctx.get()) == 1 && EVP_PKEY_keygen(ctx.get(), &raw) == 1 && raw, "ed25519 generate failed");
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
    return pemFromKey(key.get(), true);
}
std::string ed25519PublicPem(const std::string& privatePem) {
    auto key = loadPrivate(privatePem);
    return pemFromKey(key.get(), false);
}
std::string generateRsaPrivatePem() {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY* raw = nullptr;
    require(ctx && EVP_PKEY_keygen_init(ctx.get()) == 1 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) == 1 && EVP_PKEY_keygen(ctx.get(), &raw) == 1 && raw, "rsa generate failed");
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
    return pemFromKey(key.get(), true);
}
std::string rsaPublicPem(const std::string& privatePem) {
    auto key = loadPrivate(privatePem);
    return pemFromKey(key.get(), false);
}
std::string randomToken(unsigned bytes) {
    require(bytes >= 16 && bytes <= 64, "invalid token size");
    std::vector<unsigned char> raw(bytes);
    require(RAND_bytes(raw.data(), static_cast<int>(bytes)) == 1, "random failed");
    return hex(std::string(reinterpret_cast<char*>(raw.data()), raw.size()));
}
bool ensureDirectories(const std::vector<std::string>& paths, std::string& error) {
    try {
        for (const auto& path : paths) std::filesystem::create_directories(path);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
}
