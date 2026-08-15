#include <mw/version.hpp>

#include <iostream>
#include <string_view>

int main() {
    constexpr std::string_view expected_version{"0.1.0"};
    const std::string_view installed_version{mw::version()};

    std::cout << installed_version << '\n';

    if (installed_version != expected_version) {
        std::cerr << "unexpected middleware version\n";
        return 1;
    }

    return 0;
}
