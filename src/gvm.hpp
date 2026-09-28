#pragma once
#include <stdexcept>
#include <string>
#include <vector>
namespace gincy {
struct GvmCompileError {
    std::string file;
    int line = 0;
    std::string construct;
    std::string message;
    std::string hint;
};
class GvmError : public std::runtime_error {
public:
    GvmCompileError info;
    explicit GvmError(GvmCompileError value);
};
struct GvmCompileResult {
    std::string bytecode;
    std::vector<std::string> bridges;
    int instructions = 0;
    bool usedFallback = false;
};
GvmCompileResult compileClientLua(const std::string& source, const std::string& filename, bool maximum, bool allowFallback);
std::string sessionMapBytecode(const std::string& bytecode, const std::string& session);
bool isGvmBytecode(const std::string& bytes);
}
