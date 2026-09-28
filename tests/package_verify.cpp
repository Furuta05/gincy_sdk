#include "package.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        std::ifstream package(argv[1], std::ios::binary), key(argv[2], std::ios::binary);
        if (!package || !key) return 2;
        auto result = gincy::verifyPackage(std::string(std::istreambuf_iterator<char>(package), {}), std::string(std::istreambuf_iterator<char>(key), {}));
        std::cout << result.digest << " " << result.files.size() << "\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
}
