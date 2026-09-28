#include "gvm.hpp"
#include <iostream>
#include <stdexcept>
static void expect(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
int main() {
    try {
        auto hello = gincy::compileClientLua("print(1+2)\n", "hello.lua", false, false);
        expect(gincy::isGvmBytecode(hello.bytecode), "hello bytecode");
        expect(hello.instructions > 0, "hello instructions");
        expect(!hello.usedFallback, "hello fallback");
        auto mapped = gincy::sessionMapBytecode(hello.bytecode, "0123456789abcdef");
        expect(mapped != hello.bytecode, "session mapping changed opcodes");
        expect(mapped.compare(0, 4, "GVM1") == 0, "magic preserved");
        auto hud = gincy::compileClientLua("surface.DrawRect(0, 0, 64, 64)\n", "hud.lua", true, false);
        expect(gincy::isGvmBytecode(hud.bytecode), "hud bytecode");
        bool rejected = false;
        try { gincy::compileClientLua("RunString('print(1)')\n", "bad.lua", true, false); }
        catch (const gincy::GvmError&) { rejected = true; }
        expect(rejected, "maximum rejects RunString");
        bool unsupported = false;
        try { gincy::compileClientLua("goto skip\n::skip::\n", "goto.lua", true, false); }
        catch (const gincy::GvmError& error) {
            unsupported = true;
            expect(error.info.file == "goto.lua", "error file");
            expect(!error.info.construct.empty(), "error construct");
        }
        expect(unsupported, "goto rejected in maximum");
        auto fallback = gincy::compileClientLua("goto skip\n::skip::\n", "goto.lua", false, true);
        expect(fallback.usedFallback, "protected fallback allowed");
        std::cout << "PASS gvm compiler\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
