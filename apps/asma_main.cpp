// SPDX-License-Identifier: GPL-3.0-only
#include <cstring>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << "asma " << ASMA_VERSION << "\n";
        return 0;
    }
    std::cerr << "usage: asma --version\n";
    return 2;
}
