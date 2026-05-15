#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "fastcsa/fastcsa.hpp"

namespace {

struct validation_options {
    std::filesystem::path dataset_path;
    std::filesystem::path index_path;
    size_t sample_rate = 0;
    size_t num_count_patterns = 8;
    size_t pattern_len = 8;
    size_t num_extracts = 8;
    size_t extract_len = 16;
};

std::vector<uint8_t> load_dataset(const std::filesystem::path& path) {
    const size_t file_size = std::filesystem::file_size(path);
    std::vector<uint8_t> data(file_size);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) throw std::runtime_error("failed to open dataset");
    ifs.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!ifs) throw std::runtime_error("failed to read dataset");
    return data;
}

size_t naive_count(std::span<const uint8_t> text, std::span<const uint8_t> pattern) {
    if (pattern.empty() || pattern.size() > text.size()) return 0;
    size_t count = 0;
    for (size_t i = 0; i + pattern.size() <= text.size(); ++i) {
        if (std::equal(pattern.begin(), pattern.end(), text.begin() + i)) ++count;
    }
    return count;
}

template <uint32_t SampleRate>
int validate_impl(const validation_options& opts) {
    auto raw_dataset = load_dataset(opts.dataset_path);
    if (raw_dataset.size() < std::max(opts.pattern_len, opts.extract_len)) {
        throw std::runtime_error("dataset is smaller than the requested validation lengths");
    }

    fastcsa::csa csa{SampleRate};
    std::ifstream ifs(opts.index_path, std::ios::binary);
    if (!ifs) throw std::runtime_error("failed to open index");
    csa.deserialize(ifs);

    std::mt19937_64 rng(114514);
    std::uniform_int_distribution<size_t> count_dist(0, raw_dataset.size() - opts.pattern_len);
    for (size_t i = 0; i < opts.num_count_patterns; ++i) {
        const size_t start = count_dist(rng);
        const std::span<const uint8_t> pattern(raw_dataset.data() + start, opts.pattern_len);
        const size_t expected = naive_count(raw_dataset, pattern);
        const size_t actual = csa.count(pattern);
        if (expected != actual) {
            std::cerr << "count mismatch at sample " << i
                      << ": expected=" << expected
                      << ", actual=" << actual
                      << ", start=" << start << '\n';
            return 1;
        }
    }

    std::uniform_int_distribution<size_t> extract_dist(0, raw_dataset.size() - opts.extract_len);
    for (size_t i = 0; i < opts.num_extracts; ++i) {
        const size_t start = extract_dist(rng);
        const auto actual = csa.extract(start, opts.extract_len);
        if (!std::equal(actual.begin(), actual.end(), raw_dataset.begin() + start)) {
            std::cerr << "extract mismatch at sample " << i
                      << ", start=" << start << '\n';
            return 1;
        }
    }

    std::cout << "validated count samples=" << opts.num_count_patterns
              << ", extract samples=" << opts.num_extracts << '\n';
    return 0;
}

validation_options parse_args(int argc, char* argv[]) {
    if (argc < 4 || argc > 8) {
        throw std::runtime_error(
            "usage: fastcsa_dataset_validator <dataset> <index> <sample_rate> [num_count] [pattern_len] [num_extract] [extract_len]");
    }

    validation_options opts;
    opts.dataset_path = argv[1];
    opts.index_path = argv[2];
    opts.sample_rate = static_cast<size_t>(std::stoull(argv[3]));
    if (argc > 4) opts.num_count_patterns = static_cast<size_t>(std::stoull(argv[4]));
    if (argc > 5) opts.pattern_len = static_cast<size_t>(std::stoull(argv[5]));
    if (argc > 6) opts.num_extracts = static_cast<size_t>(std::stoull(argv[6]));
    if (argc > 7) opts.extract_len = static_cast<size_t>(std::stoull(argv[7]));
    return opts;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const auto opts = parse_args(argc, argv);
        switch (opts.sample_rate) {
        case 16: return validate_impl<16>(opts);
        case 32: return validate_impl<32>(opts);
        case 64: return validate_impl<64>(opts);
        case 128: return validate_impl<128>(opts);
        case 256: return validate_impl<256>(opts);
        case 512: return validate_impl<512>(opts);
        default: throw std::runtime_error("unsupported sample rate");
        }
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
