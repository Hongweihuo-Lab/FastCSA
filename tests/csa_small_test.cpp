#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <sstream>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fastcsa/fastcsa.hpp"

namespace {

size_t naive_count(std::span<const uint8_t> text, std::span<const uint8_t> pattern) {
    if (pattern.empty() || pattern.size() > text.size()) return 0;
    size_t count = 0;
    for (size_t i = 0; i + pattern.size() <= text.size(); ++i) {
        if (std::equal(pattern.begin(), pattern.end(), text.begin() + i)) ++count;
    }
    return count;
}

std::vector<uint8_t> to_bytes(std::string_view view) {
    return std::vector<uint8_t>(view.begin(), view.end());
}

} // namespace

TEST_CASE("csa count and extract match a naive reference on small text", "[csa][small]") {
    constexpr std::string_view raw = "banana_bandana";
    std::vector<uint8_t> text(raw.begin(), raw.end());
    text.push_back('\0');

    fastcsa::csa csa{4};
    csa.build(text);

    std::stringstream serialized;
    csa.serialize(serialized);

    fastcsa::csa loaded;
    loaded.deserialize(serialized);
    REQUIRE(loaded.sa_sample_rate() == 4);

    const std::array<std::string_view, 5> patterns{
        "a", "ana", "ban", "band", "na_"
    };
    const std::span<const uint8_t> raw_span(text.data(), text.size() - 1);
    for (std::string_view pattern : patterns) {
        const auto bytes = to_bytes(pattern);
        REQUIRE(loaded.count(bytes) == naive_count(raw_span, bytes));
    }

    for (size_t start = 0; start < raw.size(); ++start) {
        const size_t max_len = std::min<size_t>(4, raw.size() - start);
        for (size_t len = 1; len <= max_len; ++len) {
            const auto extracted = loaded.extract(start, len);
            REQUIRE(std::equal(extracted.begin(), extracted.end(), raw.begin() + start));
        }
    }
}
