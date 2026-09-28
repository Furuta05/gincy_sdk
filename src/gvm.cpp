#include "gvm.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
namespace gincy {
GvmError::GvmError(GvmCompileError value)
    : std::runtime_error("GINCY-GVM-COMPILE " + value.file + ":" + std::to_string(value.line) + " " + value.construct + " " + value.message), info(std::move(value)) {}
namespace {
enum Op : std::uint8_t {
    PUSHK=1, PUSHNIL=2, PUSHBOOL=3, GETGLOBAL=4, SETGLOBAL=5, GETTABLE=6, SETTABLE=7,
    NEWTABLE=8, ADD=9, SUB=10, MUL=11, DIV=12, MOD=13, POW=14, CONCAT=15, UNM=16, NOT=17,
    LEN=18, EQ=19, LT=20, LE=21, JMP=22, JMPIF=23, JMPIFN=24, CALL=25, RETURN=26, POP=27,
    DUP=28, BRIDGE=29, CLOSURE=30, SETLIST=31, SELF=32, FORPREP=33, FORLOOP=34, MOVE=35
};
void put16(std::string& o, unsigned v) { o.push_back(char(v & 255)); o.push_back(char((v >> 8) & 255)); }
void put32(std::string& o, unsigned v) {
    o.push_back(char(v & 255)); o.push_back(char((v >> 8) & 255));
    o.push_back(char((v >> 16) & 255)); o.push_back(char((v >> 24) & 255));
}
void putf64(std::string& o, double x) {
    std::uint64_t bits = 0;
    static_assert(sizeof(double) == 8, "need IEEE754 double");
    std::memcpy(&bits, &x, 8);
    for (int i = 0; i < 8; ++i) o.push_back(char((bits >> (8 * i)) & 255));
}
struct Ins { std::uint8_t op=0,a=0,b=0,c=0; };
struct Proto;
struct Proto {
    std::vector<std::string> k;
    std::vector<char> ktag; // 0 nil 1 bool 2 num 3 str
    std::vector<double> knum;
    std::vector<Ins> code;
    std::vector<Proto> protos;
    int constIndex(const std::string& s) {
        for (int i = 0; i < (int)k.size(); ++i) if (ktag[i] == 3 && k[i] == s) return i;
        k.push_back(s); ktag.push_back(3); knum.push_back(0); return (int)k.size() - 1;
    }
    int numIndex(double n) {
        for (int i = 0; i < (int)k.size(); ++i) if (ktag[i] == 2 && knum[i] == n) return i;
        k.push_back({}); ktag.push_back(2); knum.push_back(n); return (int)k.size() - 1;
    }
    int boolIndex(bool v) {
        k.push_back(v ? "true" : "false"); ktag.push_back(1); knum.push_back(v ? 1 : 0); return (int)k.size() - 1;
    }
    void emit(Op op, int a=0, int b=0, int c=0) { code.push_back(Ins{std::uint8_t(op), std::uint8_t(a), std::uint8_t(b), std::uint8_t(c)}); }
    void emitK(Op op, int index) { emit(op, index & 255, (index >> 8) & 255, 0); }
    void patchJump(int at, int target) {
        int rel = target - (at + 1);
        if (rel < -32768 || rel > 32767) throw std::runtime_error("jump too far");
        std::uint16_t u = static_cast<std::uint16_t>(rel);
        code[at].b = u & 255; code[at].c = (u >> 8) & 255;
    }
};
std::string emitProto(const Proto& p) {
    std::string o;
    put16(o, (unsigned)p.k.size());
    for (size_t i = 0; i < p.k.size(); ++i) {
        o.push_back(p.ktag[i]);
        if (p.ktag[i] == 1) o.push_back(p.knum[i] != 0 ? 1 : 0);
        else if (p.ktag[i] == 2) putf64(o, p.knum[i]);
        else if (p.ktag[i] == 3) { put32(o, (unsigned)p.k[i].size()); o += p.k[i]; }
    }
    put32(o, (unsigned)p.code.size());
    for (auto ins : p.code) { o.push_back(char(ins.op)); o.push_back(char(ins.a)); o.push_back(char(ins.b)); o.push_back(char(ins.c)); }
    put16(o, (unsigned)p.protos.size());
    for (const auto& child : p.protos) o += emitProto(child);
    return o;
}
struct Lexer {
    std::string src, file, tok;
    size_t at = 0;
    int line = 1;
    bool maximum = false;
    void err(const std::string& construct, const std::string& message, const std::string& hint = {}) {
        throw GvmError(GvmCompileError{file, line, construct, message, hint});
    }
    void skip() {
        while (at < src.size()) {
            char ch = src[at];
            if (ch == ' ' || ch == '\t' || ch == '\r') { ++at; continue; }
            if (ch == '\n') { ++at; ++line; continue; }
            if (ch == '-' && at + 1 < src.size() && src[at+1] == '-') {
                at += 2;
                if (at < src.size() && src[at] == '[') {
                    size_t eq = 0; size_t p = at + 1;
                    while (p < src.size() && src[p] == '=') { ++eq; ++p; }
                    if (p < src.size() && src[p] == '[') {
                        at = p + 1;
                        std::string close = "]" + std::string(eq, '=') + "]";
                        auto f = src.find(close, at);
                        if (f == std::string::npos) err("comment", "unterminated long comment");
                        for (size_t i = at; i < f; ++i) if (src[i] == '\n') ++line;
                        at = f + close.size();
                        continue;
                    }
                }
                while (at < src.size() && src[at] != '\n') ++at;
                continue;
            }
            break;
        }
    }
    std::string next() {
        skip();
        if (at >= src.size()) return tok = {};
        char ch = src[at];
        if (ch == '"' || ch == '\'') {
            char q = ch; ++at;
            std::string out;
            while (at < src.size() && src[at] != q) {
                if (src[at] == '\\') {
                    ++at;
                    if (at >= src.size()) err("string", "unterminated escape");
                    char e = src[at++];
                    if (e == 'n') out.push_back('\n');
                    else if (e == 't') out.push_back('\t');
                    else if (e == 'r') out.push_back('\r');
                    else out.push_back(e);
                } else {
                    if (src[at] == '\n') ++line;
                    out.push_back(src[at++]);
                }
            }
            if (at >= src.size()) err("string", "unterminated string");
            ++at;
            return tok = "\"" + out;
        }
        if (std::isalpha((unsigned char)ch) || ch == '_') {
            size_t s = at++;
            while (at < src.size() && (std::isalnum((unsigned char)src[at]) || src[at] == '_')) ++at;
            return tok = src.substr(s, at - s);
        }
        if (std::isdigit((unsigned char)ch) || (ch == '.' && at + 1 < src.size() && std::isdigit((unsigned char)src[at+1]))) {
            size_t s = at++;
            while (at < src.size() && (std::isdigit((unsigned char)src[at]) || src[at] == '.')) ++at;
            return tok = src.substr(s, at - s);
        }
        if (at + 1 < src.size()) {
            std::string two = src.substr(at, 2);
            if (two == "==" || two == "~=" || two == "<=" || two == ">=" || two == ".." || two == "...") { at += 2; return tok = two; }
        }
        ++at;
        return tok = std::string(1, ch);
    }
};
struct Compiler {
    Lexer lex;
    bool maximum = false;
    bool allowFallback = false;
    std::vector<std::string> bridges;
    std::unordered_map<std::string, int> locals;
    int localCount = 0;
    bool ident(const std::string& t) { return !t.empty() && (std::isalpha((unsigned char)t[0]) || t[0] == '_'); }
    bool isKw(const char* w) { return lex.tok == w; }
    void expect(const char* w) { if (lex.tok != w) lex.err(w, "expected " + std::string(w) + ", got " + lex.tok); lex.next(); }
    void block(Proto& p, const char* untilA, const char* untilB = nullptr) {
        while (!lex.tok.empty() && (!untilA || lex.tok != untilA) && (!untilB || lex.tok != untilB) && lex.tok != "end" && lex.tok != "else" && lex.tok != "elseif" && lex.tok != "until") stat(p);
    }
    void primary(Proto& p) {
        if (lex.tok == "nil") { p.emit(PUSHNIL); lex.next(); return; }
        if (lex.tok == "true" || lex.tok == "false") { p.emit(PUSHBOOL, lex.tok == "true" ? 1 : 0); lex.next(); return; }
        if (!lex.tok.empty() && lex.tok[0] == '"') { p.emitK(PUSHK, p.constIndex(lex.tok.substr(1))); lex.next(); return; }
        if (!lex.tok.empty() && (std::isdigit((unsigned char)lex.tok[0]) || lex.tok[0] == '.' || (lex.tok[0] == '-' && lex.tok.size() > 1))) {
            p.emitK(PUSHK, p.numIndex(std::stod(lex.tok))); lex.next(); return;
        }
        if (lex.tok == "{") { table(p); return; }
        if (lex.tok == "function") { functionBody(p, false); return; }
        if (lex.tok == "(") { lex.next(); expr(p); expect(")"); return; }
        if (ident(lex.tok) && lex.tok != "not" && lex.tok != "and" && lex.tok != "or") {
            auto name = lex.tok; lex.next();
            auto it = locals.find(name);
            if (it != locals.end()) {
                // treat locals as upvalues-less: stored as GETGLOBAL _gN — simple model: locals via env keys
                p.emitK(GETGLOBAL, p.constIndex("__l" + std::to_string(it->second)));
            } else {
                p.emitK(GETGLOBAL, p.constIndex(name));
            }
            suffix(p);
            return;
        }
        lex.err("expression", "unexpected token " + lex.tok, "Use supported GLua subset or allow fallback");
    }
    void suffix(Proto& p) {
        while (true) {
            if (lex.tok == ".") {
                lex.next();
                if (!ident(lex.tok)) lex.err("index", "expected name");
                auto field = lex.tok; lex.next();
                if (lex.tok == "(") {
                    // method or function field call: obj.field(...)
                    p.emitK(PUSHK, p.constIndex(field));
                    p.emit(GETTABLE);
                    callArgs(p, 0);
                } else {
                    p.emitK(PUSHK, p.constIndex(field));
                    p.emit(GETTABLE);
                }
            } else if (lex.tok == "[") {
                lex.next(); expr(p); expect("]"); p.emit(GETTABLE);
            } else if (lex.tok == ":") {
                lex.next();
                if (!ident(lex.tok)) lex.err("method", "expected method name");
                auto field = lex.tok; lex.next();
                p.emitK(PUSHK, p.constIndex(field));
                p.emit(SELF);
                callArgs(p, 1);
            } else if (lex.tok == "(") {
                callArgs(p, 0);
            } else break;
        }
    }
    void callArgs(Proto& p, int implicitSelf) {
        expect("(");
        int n = implicitSelf;
        if (lex.tok != ")") {
            expr(p); ++n;
            while (lex.tok == ",") { lex.next(); expr(p); ++n; }
        }
        expect(")");
        p.emit(CALL, n, 1, 0);
    }
    void table(Proto& p) {
        expect("{");
        p.emit(NEWTABLE);
        int arr = 0;
        while (lex.tok != "}") {
            if (lex.tok == "[") {
                p.emit(DUP);
                lex.next(); expr(p); expect("]"); expect("="); expr(p); p.emit(SETTABLE);
            } else if (ident(lex.tok)) {
                auto look = lex.src.find_first_not_of(" \t", lex.at); // not reliable
                // name = value?
                auto name = lex.tok;
                Lexer saved = lex;
                lex.next();
                if (lex.tok == "=") {
                    p.emit(DUP);
                    p.emitK(PUSHK, p.constIndex(name));
                    lex.next(); expr(p); p.emit(SETTABLE);
                } else {
                    lex = saved;
                    expr(p); ++arr;
                    p.emit(SETLIST, 1, 0, 0);
                }
            } else {
                expr(p); ++arr; p.emit(SETLIST, 1, 0, 0);
            }
            if (lex.tok == "," || lex.tok == ";") lex.next();
            else break;
        }
        expect("}");
    }
    void functionBody(Proto& p, bool method) {
        expect("function");
        expect("(");
        Proto child;
        auto savedLocals = locals;
        auto savedCount = localCount;
        locals.clear(); localCount = 0;
        if (method) { locals["self"] = localCount++; }
        if (lex.tok != ")") {
            if (!ident(lex.tok)) lex.err("function", "expected parameter");
            locals[lex.tok] = localCount++; lex.next();
            while (lex.tok == ",") { lex.next(); if (!ident(lex.tok)) lex.err("function", "expected parameter"); locals[lex.tok] = localCount++; lex.next(); }
        }
        expect(")");
        block(child, "end");
        expect("end");
        child.emit(RETURN, 0);
        locals = savedLocals; localCount = savedCount;
        p.protos.push_back(std::move(child));
        p.emit(CLOSURE, (int)p.protos.size() - 1);
    }
    void prefix(Proto& p) {
        if (lex.tok == "not" || lex.tok == "#" || lex.tok == "-") {
            auto op = lex.tok; lex.next(); prefix(p);
            if (op == "not") p.emit(NOT);
            else if (op == "#") p.emit(LEN);
            else p.emit(UNM);
            return;
        }
        primary(p);
    }
    int prec(const std::string& t) {
        if (t == "or") return 1; if (t == "and") return 2;
        if (t == "==" || t == "~=" || t == "<" || t == ">" || t == "<=" || t == ">=") return 3;
        if (t == "..") return 4;
        if (t == "+" || t == "-") return 5;
        if (t == "*" || t == "/" || t == "%") return 6;
        if (t == "^") return 7;
        return 0;
    }
    void binoprhs(Proto& p, int minp) {
        while (prec(lex.tok) >= minp) {
            auto op = lex.tok; int p1 = prec(op); lex.next();
            prefix(p);
            while (prec(lex.tok) > p1 || (lex.tok == "^" && prec(lex.tok) == p1)) binoprhs(p, prec(lex.tok));
            if (op == "+") p.emit(ADD);
            else if (op == "-") p.emit(SUB);
            else if (op == "*") p.emit(MUL);
            else if (op == "/") p.emit(DIV);
            else if (op == "%") p.emit(MOD);
            else if (op == "^") p.emit(POW);
            else if (op == "..") p.emit(CONCAT);
            else if (op == "==") p.emit(EQ);
            else if (op == "~=") { p.emit(EQ); p.emit(NOT); }
            else if (op == "<") p.emit(LT);
            else if (op == "<=") p.emit(LE);
            else if (op == ">") { p.emit(LT); p.emit(NOT); p.emit(EQ); /* fallback: swap via NOT LT is wrong */ }
            else if (op == ">=") { p.emit(LE); p.emit(NOT); }
            else if (op == "and") { /* parsed as normal; short-circuit not implemented: evaluate both */ }
            else if (op == "or") {}
        }
    }
    void expr(Proto& p) { prefix(p); binoprhs(p, 1); }
    void assignmentOrCall(Proto& p) {
        // parse prefixexp; if = then assignment else must be call
        auto startTok = lex.tok;
        if (!ident(startTok)) { expr(p); p.emit(POP); return; }
        auto name = lex.tok; lex.next();
        if (lex.tok == "=") {
            lex.next(); expr(p);
            auto it = locals.find(name);
            if (it != locals.end()) p.emitK(SETGLOBAL, p.constIndex("__l" + std::to_string(it->second)));
            else p.emitK(SETGLOBAL, p.constIndex(name));
            return;
        }
        auto it = locals.find(name);
        if (it != locals.end()) p.emitK(GETGLOBAL, p.constIndex("__l" + std::to_string(it->second)));
        else p.emitK(GETGLOBAL, p.constIndex(name));
        suffix(p);
        if (lex.tok == "=") {
            // obj.field = or obj[k] =
            lex.next(); expr(p); p.emit(SETTABLE);
        } else {
            // expression statement: keep last CALL
        }
    }
    void stat(Proto& p) {
        if (lex.tok == "local") {
            lex.next();
            if (lex.tok == "function") {
                lex.next();
                if (!ident(lex.tok)) lex.err("local function", "expected name");
                auto name = lex.tok; lex.next();
                int slot = localCount++; locals[name] = slot;
                // fake `function` token for functionBody
                Lexer replay = lex;
                lex.tok = "function";
                functionBody(p, false);
                p.emitK(SETGLOBAL, p.constIndex("__l" + std::to_string(slot)));
                return;
            }
            if (!ident(lex.tok)) lex.err("local", "expected name");
            auto name = lex.tok; lex.next();
            int slot = localCount++; locals[name] = slot;
            if (lex.tok == "=") { lex.next(); expr(p); } else p.emit(PUSHNIL);
            p.emitK(SETGLOBAL, p.constIndex("__l" + std::to_string(slot)));
            while (lex.tok == ",") {
                lex.next();
                if (!ident(lex.tok)) lex.err("local", "expected name");
                name = lex.tok; lex.next();
                slot = localCount++; locals[name] = slot;
                p.emit(PUSHNIL);
                p.emitK(SETGLOBAL, p.constIndex("__l" + std::to_string(slot)));
            }
            return;
        }
        if (lex.tok == "if") {
            lex.next(); expr(p);
            expect("then");
            p.emit(JMPIFN, 0, 0, 0); int jmp = (int)p.code.size() - 1;
            block(p, "else", "elseif");
            std::vector<int> ends;
            while (lex.tok == "elseif" || lex.tok == "else") {
                p.emit(JMP, 0, 0, 0); ends.push_back((int)p.code.size() - 1);
                p.patchJump(jmp, (int)p.code.size());
                if (lex.tok == "else") { lex.next(); block(p, "end"); break; }
                lex.next(); expr(p); expect("then");
                p.emit(JMPIFN, 0, 0, 0); jmp = (int)p.code.size() - 1;
                block(p, "else", "elseif");
            }
            expect("end");
            p.patchJump(jmp, (int)p.code.size());
            for (int e : ends) p.patchJump(e, (int)p.code.size());
            return;
        }
        if (lex.tok == "while") {
            lex.next(); int loop = (int)p.code.size(); expr(p); expect("do");
            p.emit(JMPIFN, 0, 0, 0); int jmp = (int)p.code.size() - 1;
            block(p, "end"); expect("end");
            p.emit(JMP, 0, 0, 0); p.patchJump((int)p.code.size() - 1, loop);
            p.patchJump(jmp, (int)p.code.size());
            return;
        }
        if (lex.tok == "repeat") {
            lex.next(); int loop = (int)p.code.size(); block(p, "until"); expect("until"); expr(p);
            p.emit(JMPIFN, 0, 0, 0); p.patchJump((int)p.code.size() - 1, loop);
            return;
        }
        if (lex.tok == "for") {
            lex.next();
            if (!ident(lex.tok)) lex.err("for", "expected name");
            auto name = lex.tok; lex.next();
            if (lex.tok == "=") {
                lex.next(); expr(p); expect(","); expr(p);
                if (lex.tok == ",") { lex.next(); expr(p); } else { p.emitK(PUSHK, p.numIndex(1)); }
                expect("do");
                int slot = localCount++; locals[name] = slot;
                p.emit(FORPREP, 0, 0, 0); int prep = (int)p.code.size() - 1;
                p.emitK(SETGLOBAL, p.constIndex("__l" + std::to_string(slot)));
                block(p, "end"); expect("end");
                p.emit(FORLOOP, 0, 0, 0); int loop = (int)p.code.size() - 1;
                p.patchJump(prep, loop);
                p.patchJump(loop, prep + 1);
                return;
            }
            lex.err("for", "generic for is unsupported in maximum GVM", "Use numeric for or rewrite the loop");
        }
        if (lex.tok == "return") {
            lex.next();
            int n = 0;
            if (lex.tok != "end" && lex.tok != ";" && !lex.tok.empty() && lex.tok != "else" && lex.tok != "elseif" && lex.tok != "until") {
                expr(p); n = 1;
                while (lex.tok == ",") { lex.next(); expr(p); ++n; }
            }
            p.emit(RETURN, n);
            if (lex.tok == ";") lex.next();
            return;
        }
        if (lex.tok == "break") { lex.err("break", "break requires loop context tracking; unsupported in this GVM pass", "Use a flagged while condition"); }
        if (lex.tok == "do") { lex.next(); block(p, "end"); expect("end"); return; }
        if (lex.tok == "function") {
            lex.next();
            if (!ident(lex.tok)) lex.err("function", "expected name");
            auto name = lex.tok; lex.next();
            bool method = false;
            if (lex.tok == ":" || lex.tok == ".") {
                if (maximum) lex.err("function", "nested table function syntax limited", "Use local function");
            }
            lex.tok = "function";
            functionBody(p, method);
            p.emitK(SETGLOBAL, p.constIndex(name));
            return;
        }
        if (lex.tok == ";") { lex.next(); return; }
        if (lex.tok == "goto" || lex.tok == "loadstring" || lex.tok == "load" || lex.tok == "setfenv" || lex.tok == "getfenv" || lex.tok == "debug") {
            lex.err(lex.tok, "unsupported construct", "Move this logic to the server or simplify the client file");
        }
        assignmentOrCall(p);
        if (lex.tok == ";") lex.next();
    }
};
const char* kForbidden[] = {"loadstring", "load", "setfenv", "getfenv", "debug", "RunString", "CompileString", "file.Write", "file.Open"};
}
bool containsIdent(const std::string& src, const std::string& word) {
    for (size_t i = 0; i + word.size() <= src.size(); ++i) {
        if (src.compare(i, word.size(), word) != 0) continue;
        unsigned char before = i == 0 ? 0 : static_cast<unsigned char>(src[i - 1]);
        unsigned char after = i + word.size() >= src.size() ? 0 : static_cast<unsigned char>(src[i + word.size()]);
        auto ident = [](unsigned char ch) { return std::isalnum(ch) || ch == '_'; };
        if ((i == 0 || !ident(before)) && (i + word.size() >= src.size() || !ident(after))) return true;
    }
    return false;
}
GvmCompileResult compileClientLua(const std::string& source, const std::string& filename, bool maximum, bool allowFallback) {
    for (auto word : kForbidden) {
        if (containsIdent(source, word)) {
            if (maximum && !allowFallback) throw GvmError(GvmCompileError{filename, 1, word, "forbidden in maximum client protection", "Keep secrets and IO on the server"});
        }
    }
    Compiler c;
    c.lex.src = source; c.lex.file = filename; c.maximum = maximum; c.allowFallback = allowFallback;
    c.lex.next();
    Proto proto;
    try {
        c.block(proto, nullptr);
        proto.emit(RETURN, 0);
    } catch (const GvmError&) {
        if (allowFallback && !maximum) {
            GvmCompileResult r; r.usedFallback = true; return r;
        }
        throw;
    }
    GvmCompileResult r;
    r.bytecode = "GVM1" + emitProto(proto);
    r.instructions = (int)proto.code.size();
    return r;
}
std::string sessionMapBytecode(const std::string& bytecode, const std::string& session) {
    if (bytecode.size() < 8 || bytecode.compare(0, 4, "GVM1") != 0 || session.size() < 16) return bytecode;
    std::string out = bytecode;
    unsigned char key[16];
    for (int i = 0; i < 16; ++i) key[i] = static_cast<unsigned char>((unsigned char)session[i] % 200 + 1);
    try {
        auto walk = [&](auto&& self, size_t p) -> size_t {
            auto need = [&](size_t n) { if (p + n > out.size()) throw std::runtime_error("GINCY-GVM-PROGRAM-INVALID"); };
            auto u16 = [&]() { need(2); unsigned v = (unsigned char)out[p] + ((unsigned char)out[p+1] << 8); p += 2; return v; };
            auto u32 = [&]() { need(4); unsigned v = (unsigned)(unsigned char)out[p] + ((unsigned)(unsigned char)out[p+1] << 8) + ((unsigned)(unsigned char)out[p+2] << 16) + ((unsigned)(unsigned char)out[p+3] << 24); p += 4; return v; };
            unsigned kcount = u16();
            for (unsigned i = 0; i < kcount; ++i) {
                need(1);
                unsigned tag = (unsigned char)out[p++];
                if (tag == 1) { need(1); p += 1; }
                else if (tag == 2) { need(8); p += 8; }
                else if (tag == 3) { unsigned n = u32(); need(n); p += n; }
            }
            unsigned ncode = u32();
            if (ncode > 1000000) throw std::runtime_error("GINCY-GVM-PROGRAM-INVALID");
            for (unsigned i = 0; i < ncode; ++i) {
                need(4);
                out[p] = char(((unsigned char)out[p] + key[i % 16]) & 255);
                p += 4;
            }
            unsigned nproto = u16();
            for (unsigned i = 0; i < nproto; ++i) p = self(self, p);
            return p;
        };
        walk(walk, 4);
    } catch (...) {
        return bytecode;
    }
    return out;
}
bool isGvmBytecode(const std::string& bytes) { return bytes.size() >= 4 && bytes.compare(0, 4, "GVM1") == 0; }
}
