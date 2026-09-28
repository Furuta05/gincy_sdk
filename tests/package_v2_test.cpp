#include "package.hpp"
#include "json.hpp"
#include "gvm.hpp"
#include <iostream>
#include <stdexcept>
static void expect(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
int main() {
    try {
        auto sign = gincy::generateEd25519PrivatePem();
        auto pub = gincy::ed25519PublicPem(sign);
        auto recipient = gincy::generateRsaPrivatePem();
        auto recipientPub = gincy::rsaPublicPem(recipient);
        gincy::PackRequest open;
        open.manifestJson = "{\"id\":\"sample\",\"name\":\"Sample\",\"version\":\"1.0.0\"}";
        open.files.push_back({"sv_init.lua", 1, "return 1\n"});
        open.files.push_back({"cl_init.lua", 3, "surface.DrawRect(0,0,1,1)\n"});
        open.privateKeyPem = sign;
        open.allowUnpack = true;
        auto openBytes = gincy::packPackage(open);
        auto inspected = gincy::inspectPackage(openBytes);
        expect(inspected.formatVersion == 2, "format 2");
        auto verified = gincy::verifyPackage(openBytes, pub);
        expect(verified.files.size() == 2, "open files");
        auto tampered = openBytes;
        tampered[tampered.size() / 2] ^= 1;
        bool badSig = false;
        try { gincy::verifyPackage(tampered, pub); } catch (const std::exception& error) {
            badSig = std::string(error.what()).find("GINCY-PKG-SIGNATURE-INVALID") != std::string::npos
                || std::string(error.what()).find("GINCY-PKG-INTEGRITY") != std::string::npos
                || std::string(error.what()).find("untrusted") != std::string::npos;
        }
        expect(badSig, "tamper rejected");
        gincy::PackRequest maximum;
        maximum.manifestJson = "{\"id\":\"modern_inventory\",\"name\":\"ModernInventory\",\"version\":\"2.1.0\",\"protection\":{\"server\":\"maximum\",\"client\":\"maximum\",\"drm\":false}}";
        maximum.files.push_back({"sv_core.lua", 1, "-- secret\nreturn 42\n"});
        maximum.files.push_back({"cl_hud.lua", 3, "surface.DrawRect(0,0,8,8)\n"});
        maximum.privateKeyPem = sign;
        maximum.serverProtection = "maximum";
        maximum.clientProtection = "maximum";
        maximum.fingerprint = "build";
        auto maxBytes = gincy::packPackage(maximum);
        expect(maximum.report.find("Gincy Protection Report") != std::string::npos, "report");
        auto maxPkg = gincy::verifyPackage(maxBytes, pub);
        expect(maxPkg.files.size() == 2, "max files");
        bool sawServer = false, sawGvm = false;
        for (const auto& entry : maxPkg.files) {
            expect(entry.protectedPayload, "protected entry");
            if (entry.path == "sv_core.lua") {
                expect(entry.protectionKind == 2, "unbound server");
                auto plain = gincy::decryptEntry(entry, {}, maxPkg.signerRaw);
                expect(plain.find("return 42") != std::string::npos, "server decrypt");
                expect(plain.find("secret") == std::string::npos, "comments stripped");
                auto corrupted = entry;
                corrupted.data.back() ^= 1;
                bool auth = false;
                try { gincy::decryptEntry(corrupted, {}, maxPkg.signerRaw); } catch (...) { auth = true; }
                expect(auth, "gcm tag");
                sawServer = true;
            }
            if (entry.path == "cl_hud.lua") {
                expect(entry.protectionKind == 3, "unbound gvm");
                auto program = gincy::decryptEntry(entry, {}, maxPkg.signerRaw);
                expect(gincy::isGvmBytecode(program), "client is GVM");
                expect(program.find("surface.DrawRect") == std::string::npos, "no plaintext GLua");
                sawGvm = true;
            }
        }
        expect(sawServer && sawGvm, "entry kinds");
        auto meta = gincy::Json::parse(maxPkg.manifest);
        expect(meta.find("protection_policy") != nullptr, "signed policy");
        expect(meta.find("fingerprint") != nullptr, "fingerprint");
        gincy::PackRequest bound;
        bound.manifestJson = "{\"id\":\"bound\",\"version\":\"1.0.0\"}";
        bound.files.push_back({"sv_db.lua", 1, "return 'db'\n"});
        bound.privateKeyPem = sign;
        bound.recipientPem = recipientPub;
        bound.serverProtection = "protected";
        auto boundBytes = gincy::packPackage(bound);
        auto boundPkg = gincy::verifyPackage(boundBytes, pub);
        auto plain = gincy::decryptEntry(boundPkg.files[0], recipient, boundPkg.signerRaw);
        expect(plain.find("db") != std::string::npos, "recipient decrypt");
        auto other = gincy::generateRsaPrivatePem();
        bool wrong = false;
        try { gincy::decryptEntry(boundPkg.files[0], other, boundPkg.signerRaw); } catch (const std::exception& error) {
            wrong = std::string(error.what()).find("GINCY-PKG-RECIPIENT-MISMATCH") != std::string::npos
                || std::string(error.what()).find("mismatch") != std::string::npos;
        }
        expect(wrong, "wrong recipient");
        auto truncated = maxBytes.substr(0, maxBytes.size() - 8);
        bool trunc = false;
        try { gincy::verifyPackage(truncated, pub); } catch (...) { trunc = true; }
        expect(trunc, "truncated package");
        std::string garbage = "NOTAGMOD" + maxBytes.substr(8);
        bool format = false;
        try { gincy::inspectPackage(garbage); } catch (const std::exception& error) {
            format = std::string(error.what()).find("GINCY-PKG-FORMAT-UNSUPPORTED") != std::string::npos;
        }
        expect(format, "bad magic");
        std::cout << "PASS package v2\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
