#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fastcsa/fastcsa.hpp"

namespace {

using value_type = uint64_t;
using list_type = _Fastcsa_internal::_H<value_type>;

struct encoded_re {
    std::vector<uint64_t> blocks;
    uint8_t ext_meta = 0;
};

encoded_re encode_re(const std::vector<value_type>& values) {
    auto encoder = _Fastcsa_m_internal::_Re::new_encoder(values.begin(), values.size());
    const size_t required_bits = yaef::details::bits64::align_to<64>(encoder->required_bits());
    encoded_re out;
    out.blocks.assign(required_bits / 64, 0);
    encoder->encode(reinterpret_cast<uint8_t*>(out.blocks.data()), out.ext_meta);
    return out;
}

std::vector<value_type> make_runs(std::span<const uint16_t> starts, uint64_t gap_base = 17) {
    std::vector<value_type> values;
    values.reserve(256);
    uint64_t run_start = 0;
    for (size_t i = 0; i < starts.size(); ++i) {
        if (i != 0) run_start += gap_base + i * 13;
        const size_t len = (i + 1 < starts.size() ? starts[i + 1] : 256) - starts[i];
        for (size_t j = 0; j < len; ++j) values.push_back(run_start + j);
        run_start += len;
    }
    return values;
}

std::vector<value_type> make_tail_runs(size_t n) {
    std::vector<value_type> values;
    values.reserve(n);
    uint64_t cur = 0;
    while (values.size() < n) {
        const size_t rem = n - values.size();
        const size_t len = std::min<size_t>(rem, values.empty() ? 19 : 23);
        for (size_t i = 0; i < len; ++i) values.push_back(cur + i);
        cur += len + 31;
    }
    return values;
}

std::vector<value_type> make_large_width_runs() {
    return {
        0,
        (uint64_t{1} << 63),
        (uint64_t{1} << 63) + 1,
        std::numeric_limits<uint64_t>::max() - 3
    };
}

} // namespace

TEST_CASE("re direct encoding supports random access and lower_bound on full blocks", "[re][unit]") {
    constexpr std::array<uint16_t, 5> starts{0, 4, 7, 128, 190};
    const auto values = make_runs(starts, 257);
    const auto encoded = encode_re(values);
    const auto* buf = reinterpret_cast<const uint8_t*>(encoded.blocks.data());

    for (size_t i = 0; i < values.size(); ++i) {
        uint64_t actual = 0;
        _Fastcsa_m_internal::_Re::at(i, buf, encoded.ext_meta, &actual);
        REQUIRE(actual == values[i]);
    }

    for (uint64_t target = 0; target <= values.back() + 4; ++target) {
        size_t actual = 0;
        _Fastcsa_m_internal::_Re::index_of_lower_bound(target, buf, encoded.ext_meta, &actual);
        const size_t expected = std::lower_bound(values.begin(), values.end(), target) - values.begin();
        REQUIRE(actual == expected);
    }
}

TEST_CASE("re handles run starts around 64-bit boundaries", "[re][unit]") {
    constexpr std::array<uint16_t, 8> starts{0, 63, 64, 127, 128, 191, 192, 255};
    const auto values = make_runs(starts, 29);
    const auto encoded = encode_re(values);
    const auto* buf = reinterpret_cast<const uint8_t*>(encoded.blocks.data());

    for (size_t i = 0; i < values.size(); ++i) {
        uint64_t actual = 0;
        _Fastcsa_m_internal::_Re::at(i, buf, encoded.ext_meta, &actual);
        REQUIRE(actual == values[i]);
    }

    for (size_t i = 0; i < starts.size(); ++i) {
        const size_t pos = starts[i];
        for (uint64_t delta = 0; delta < 5; ++delta) {
            const uint64_t target = values[pos] + delta;
            size_t actual = 0;
            _Fastcsa_m_internal::_Re::index_of_lower_bound(target, buf, encoded.ext_meta, &actual);
            const size_t expected = std::lower_bound(values.begin(), values.end(), target) - values.begin();
            REQUIRE(actual == expected);
        }
    }
}

TEST_CASE("re supports wide low parts", "[re][unit]") {
    const auto values = make_large_width_runs();
    const auto encoded = encode_re(values);
    const auto* buf = reinterpret_cast<const uint8_t*>(encoded.blocks.data());

    for (size_t i = 0; i < values.size(); ++i) {
        uint64_t actual = 0;
        _Fastcsa_m_internal::_Re::at(i, buf, encoded.ext_meta, &actual);
        REQUIRE(actual == values[i]);
    }

    const std::array<uint64_t, 6> targets{
        0,
        1,
        (uint64_t{1} << 63),
        (uint64_t{1} << 63) + 1,
        std::numeric_limits<uint64_t>::max() - 4,
        std::numeric_limits<uint64_t>::max() - 3
    };
    for (uint64_t target : targets) {
        size_t actual = 0;
        _Fastcsa_m_internal::_Re::index_of_lower_bound(target, buf, encoded.ext_meta, &actual);
        const size_t expected = std::lower_bound(values.begin(), values.end(), target) - values.begin();
        REQUIRE(actual == expected);
    }
}

TEST_CASE("hybrid list keeps tail partitions correct", "[re][hybrid]") {
    const auto data = make_tail_runs(173);
    const list_type list(yaef::from_sorted, data.begin(), data.end());

    for (size_t i = 0; i < data.size(); ++i) REQUIRE(list.at(i) == data[i]);
    for (uint64_t target = 0; target <= data.back() + 16; ++target) {
        const size_t expected = std::lower_bound(data.begin(), data.end(), target) - data.begin();
        REQUIRE(list.index_of_lower_bound(target) == expected);
    }
}

TEST_CASE("hybrid list selects re on run-heavy full partitions", "[re][hybrid]") {
    constexpr std::array<uint16_t, 4> starts{0, 80, 160, 224};
    const auto data = make_runs(starts, 4096);
    const list_type list(yaef::from_sorted, data.begin(), data.end());
    const auto stats = list.method_stats();
    const auto re_iter = std::find_if(stats.begin(), stats.end(), [](const auto& entry) {
        return entry.name == "RE";
    });
    REQUIRE(re_iter != stats.end());
    REQUIRE(re_iter->num_partitions == 1);
    REQUIRE(re_iter->encoded_elements == data.size());
}
