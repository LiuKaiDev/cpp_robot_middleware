#include <mw/config.hpp>
#include <mw/context.hpp>
#include <mw/loaned_sample.hpp>
#include <mw/sample_view.hpp>
#include <mw/version.hpp>

#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible<mw::LoanedSample>::value);
static_assert(!std::is_copy_constructible<mw::SampleView>::value);
static_assert(std::is_same<decltype(std::declval<mw::SampleView&>().data()), const void*>::value);

int main() {
    const mw::Context context{"external_consumer"};
    const mw::PublisherConfig config{};
    const mw::OverflowPolicy policy = mw::OverflowPolicy::DropOldest;
    (void)context;
    (void)config;
    (void)policy;

    constexpr std::string_view expected_version{"0.1.0"};
    const std::string_view installed_version{mw::version()};

    std::cout << installed_version << '\n';

    if (installed_version != expected_version) {
        std::cerr << "unexpected middleware version\n";
        return 1;
    }

    return 0;
}
