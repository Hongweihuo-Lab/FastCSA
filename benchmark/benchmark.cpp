#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "argparse/argparse.hpp"

#include "fastcsa/fastcsa.hpp"

using Clock = std::chrono::steady_clock;
using TimePoint = typename Clock::time_point;
using Duration = typename Clock::duration;
using MicrosDuration = std::chrono::duration<double, std::micro>;
using MillisDuration = std::chrono::duration<double, std::milli>;
using SecondsDuration = std::chrono::duration<double>;

class Benchmark {
public:
  virtual ~Benchmark() = default;

  [[nodiscard]] virtual SecondsDuration build(const std::filesystem::path &dataset_path,
                                              const std::filesystem::path &index_path) = 0;
  [[nodiscard]] virtual MillisDuration load(std::filesystem::path &index_path) = 0;
  [[nodiscard]] virtual MicrosDuration test_count(size_t num_patterns, size_t pattern_len) = 0;
  [[nodiscard]] virtual MillisDuration test_locate(size_t num_patterns, size_t pattern_len) = 0;
  [[nodiscard]] virtual MicrosDuration test_extract(size_t num_substrs, size_t substr_len) = 0;
  [[nodiscard]] virtual size_t space_usage_in_bytes() const = 0;
};

class BenchmarkImpl : public Benchmark {
public:
  BenchmarkImpl() = default;
  explicit BenchmarkImpl(size_t sample_rate) : csa_(sample_rate) {}

  SecondsDuration build(const std::filesystem::path &dataset_path, 
                        const std::filesystem::path &index_path) override {
    size_t filesize = std::filesystem::file_size(dataset_path);
    std::vector<uint8_t> buf(filesize + 1);
    std::ifstream ifs(dataset_path, std::ios::in | std::ios::binary);
    ifs.read(reinterpret_cast<char *>(buf.data()), filesize);
    buf.back() = '\0';

    TimePoint tp_beg = Clock::now();
    csa_.build(buf);
    TimePoint tp_end = Clock::now();

    std::ofstream ofs(index_path, std::ios::out | std::ios::binary);
    csa_.serialize(ofs);
    return std::chrono::duration_cast<SecondsDuration>(tp_end - tp_beg);
  }

  MillisDuration load(std::filesystem::path &index_path) override {
    std::ifstream ifs(index_path, std::ios::in | std::ios::binary);

    TimePoint tp_beg = Clock::now();
    csa_.deserialize(ifs);
    TimePoint tp_end = Clock::now();
    return std::chrono::duration_cast<MillisDuration>(tp_end - tp_beg);
  }

  MicrosDuration test_count(size_t num_patterns, size_t pattern_len) override {
    auto patterns = generate_patterns(num_patterns, pattern_len);

    TimePoint tp_beg = Clock::now();
    for (size_t i = 0; i < patterns.size(); ++i) {
      size_t num = csa_.count(patterns[i]);
      asm volatile("" : "+m,r"(num) : : "memory");
    }
    TimePoint tp_end = Clock::now();
    return std::chrono::duration_cast<MicrosDuration>(tp_end - tp_beg) / static_cast<double>(num_patterns);
  }

  MillisDuration test_locate(size_t num_patterns, size_t pattern_len) override {
    auto patterns = generate_patterns(num_patterns, pattern_len);

    TimePoint tp_beg = Clock::now();
    for (size_t i = 0; i < patterns.size(); ++i) {
      std::vector<size_t> locs = csa_.locate(patterns[i]);
    }

    TimePoint tp_end = Clock::now();
    return std::chrono::duration_cast<MillisDuration>(tp_end - tp_beg) / static_cast<double>(num_patterns);
  }

  MicrosDuration test_extract(size_t num_substrs, size_t substr_len) override {
    auto starts = generate_substr_starts(num_substrs, substr_len);

    TimePoint tp_beg = Clock::now();
    for (size_t i = 0; i < starts.size(); ++i) {
      std::vector<uint8_t> substr = csa_.extract(starts[i], substr_len);
    }
    TimePoint tp_end = Clock::now();
    return std::chrono::duration_cast<MicrosDuration>(tp_end - tp_beg) / static_cast<double>(num_substrs);
  }

  size_t space_usage_in_bytes() const override {
    return csa_.space_usage_in_bytes();
  }

private:
  fastcsa::csa csa_;

  std::vector<size_t> generate_substr_starts(size_t num_substrs, size_t substr_len) {
    static thread_local std::mt19937_64 rng(114514);
    auto dist = std::uniform_int_distribution<size_t>(0, csa_.size() - substr_len - 1);
    std::vector<size_t> starts(num_substrs);
    for (size_t i = 0; i < num_substrs; ++i) {
      starts[i] = dist(rng);
    }
    return starts;
  }

  std::vector<std::vector<uint8_t>> generate_patterns(size_t num_patterns, size_t pattern_len) {
    auto starts = generate_substr_starts(num_patterns, pattern_len);
    std::vector<std::vector<uint8_t>> patterns(num_patterns);
    for (size_t i = 0; i < num_patterns; ++i) {
      patterns[i] = csa_.extract(starts[i], pattern_len);
    }
    return patterns;
  }
};

std::vector<uint8_t> to_bytes(std::string_view view) {
  return std::vector<uint8_t>(view.begin(), view.end());
}

bool has_trailing_input(std::istream &is) {
  is >> std::ws;
  return !is.eof();
}

bool read_pattern(std::istream &is, std::string &pattern) {
  is >> std::ws;
  if (is.eof()) return false;
  if (is.peek() == '"') {
    is >> std::quoted(pattern);
  } else {
    is >> pattern;
  }
  return static_cast<bool>(is);
}

bool parse_size_token(const std::string &token, size_t &value) {
  if (token.empty() || token[0] == '-') return false;

  size_t parsed_len = 0;
  try {
    value = static_cast<size_t>(std::stoull(token, &parsed_len));
  } catch (const std::exception &) {
    return false;
  }
  return parsed_len == token.size();
}

bool read_size_arg(std::istream &is, size_t &value) {
  std::string token;
  if (!(is >> token)) return false;
  return parse_size_token(token, value);
}

std::string format_bytes(size_t bytes) {
  constexpr const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < std::size(units)) {
    value /= 1024.0;
    ++unit;
  }

  std::ostringstream out;
  if (unit == 0) {
    out << bytes << ' ' << units[unit];
  } else {
    out << std::fixed << std::setprecision(2) << value << ' ' << units[unit];
  }
  return out.str();
}

std::string format_fixed(double value, int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

std::string escape_bytes(std::span<const uint8_t> bytes) {
  std::ostringstream out;
  for (uint8_t byte : bytes) {
    const auto ch = static_cast<unsigned char>(byte);
    switch (ch) {
    case '\0': out << "\\0"; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    case '\\': out << "\\\\"; break;
    case '"': out << "\\\""; break;
    default:
      if (std::isprint(ch)) {
        out << static_cast<char>(ch);
      } else {
        out << "\\x" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(ch) << std::dec << std::setfill(' ');
      }
      break;
    }
  }
  return out.str();
}

void print_interactive_help() {
  std::cout
      << "Commands:\n"
      << "  count <pattern>\n"
      << "  locate <pattern> [limit]\n"
      << "  extract <offset> <length>\n"
      << "  space\n"
      << "  help\n"
      << "  quit | exit\n"
      << "Patterns may be unquoted when they contain no spaces, or quoted otherwise.\n";
}

void print_space_usage(const fastcsa::csa &csa, const std::filesystem::path &index_path) {
  const size_t text_size = csa.size();
  const size_t dataset_size = text_size == 0 ? 0 : text_size - 1;
  const size_t index_file_size = std::filesystem::file_size(index_path);
  const size_t index_space_usage = csa.space_usage_in_bytes();

  std::cout << "Space usage:\n"
            << "  original data size: " << dataset_size << " bytes ("
            << format_bytes(dataset_size) << ")\n"
            << "  indexed text size: " << text_size << " bytes ("
            << format_bytes(text_size) << ")\n"
            << "  index file size: " << index_file_size << " bytes ("
            << format_bytes(index_file_size) << ")\n"
            << "  index space usage: " << index_space_usage << " bytes ("
            << format_bytes(index_space_usage) << ")\n"
            << "  sample rate: " << csa.sa_sample_rate() << '\n';

  if (dataset_size == 0) {
    std::cout << "  compression ratio: n/a\n"
              << "  bits per original byte: n/a\n";
    return;
  }

  const double file_ratio = static_cast<double>(index_file_size) / static_cast<double>(dataset_size);
  const double space_ratio = static_cast<double>(index_space_usage) / static_cast<double>(dataset_size);
  const double bits_per_byte = 8.0 * static_cast<double>(index_space_usage) / static_cast<double>(dataset_size);
  std::cout << "  compression ratio (file/original): "
            << format_fixed(file_ratio, 6) << '\n'
            << "  compression ratio (space/original): "
            << format_fixed(space_ratio, 6) << '\n'
            << "  bits per original byte: "
            << format_fixed(bits_per_byte, 3) << '\n';
}

void print_locations(const std::vector<size_t> &locations, size_t limit) {
  const size_t shown = std::min(limit, locations.size());
  std::cout << "positions:";
  for (size_t i = 0; i < shown; ++i) {
    std::cout << ' ' << locations[i];
  }
  if (locations.size() > limit) {
    std::cout << " ... (" << locations.size() << " total, showing first " << limit << ')';
  } else {
    std::cout << " (" << locations.size() << " total)";
  }
  std::cout << '\n';
}

int run_interactive(const std::filesystem::path &index_path) {
  if (!std::filesystem::exists(index_path)) {
    std::cerr << "Error: Index file not found: " << index_path << std::endl;
    return 1;
  }

  fastcsa::csa csa;
  std::ifstream ifs(index_path, std::ios::in | std::ios::binary);
  if (!ifs) {
    std::cerr << "Error: Failed to open index file: " << index_path << std::endl;
    return 1;
  }

  TimePoint tp_beg = Clock::now();
  try {
    csa.deserialize(ifs);
  } catch (const std::exception &err) {
    std::cerr << "Error: Failed to load index: " << err.what() << std::endl;
    return 1;
  }
  TimePoint tp_end = Clock::now();
  auto load_time = std::chrono::duration_cast<MillisDuration>(tp_end - tp_beg);

  std::cout << "Loaded index in " << load_time.count() << " ms. "
            << "size=" << csa.size()
            << ", sample_rate=" << csa.sa_sample_rate() << '\n';
  std::cout << "Type help for commands.\n";

  std::string line;
  while (true) {
    std::cout << "fastcsa> " << std::flush;
    if (!std::getline(std::cin, line)) break;

    std::istringstream line_stream(line);
    std::string command;
    if (!(line_stream >> command)) continue;

    try {
      if (command == "quit" || command == "exit") {
        break;
      }

      if (command == "help") {
        if (has_trailing_input(line_stream)) {
          std::cerr << "Error: help does not take arguments.\n";
          continue;
        }
        print_interactive_help();
        continue;
      }

      if (command == "space") {
        if (has_trailing_input(line_stream)) {
          std::cerr << "Error: space does not take arguments.\n";
          continue;
        }
        print_space_usage(csa, index_path);
        continue;
      }

      if (command == "count") {
        std::string pattern;
        if (!read_pattern(line_stream, pattern) || pattern.empty() || has_trailing_input(line_stream)) {
          std::cerr << "Error: usage: count <pattern>\n";
          continue;
        }
        const auto pattern_bytes = to_bytes(pattern);
        std::cout << "count: " << csa.count(pattern_bytes) << '\n';
        continue;
      }

      if (command == "locate") {
        std::string pattern;
        if (!read_pattern(line_stream, pattern) || pattern.empty()) {
          std::cerr << "Error: usage: locate <pattern> [limit]\n";
          continue;
        }

        size_t limit = 10;
        if (has_trailing_input(line_stream)) {
          if (!read_size_arg(line_stream, limit) || has_trailing_input(line_stream)) {
            std::cerr << "Error: usage: locate <pattern> [limit]\n";
            continue;
          }
        }

        const auto pattern_bytes = to_bytes(pattern);
        auto locations = csa.locate(pattern_bytes, true);
        print_locations(locations, limit);
        continue;
      }

      if (command == "extract") {
        size_t offset = 0;
        size_t length = 0;
        if (!read_size_arg(line_stream, offset) || !read_size_arg(line_stream, length) ||
            has_trailing_input(line_stream)) {
          std::cerr << "Error: usage: extract <offset> <length>\n";
          continue;
        }

        if (length == 0) {
          std::cout << "text: \"\"\n";
          continue;
        }

        const auto extracted = csa.extract(offset, length);
        std::cout << "text: \"" << escape_bytes(extracted) << "\"\n";
        continue;
      }

      std::cerr << "Error: unknown command '" << command << "'. Type help for commands.\n";
    } catch (const std::exception &err) {
      std::cerr << "Error: " << err.what() << '\n';
    }
  }

  return 0;
}

int main(int argc, char *argv[]) {
  argparse::ArgumentParser program("benchmark", "1.0");

  argparse::ArgumentParser build_command("build");
  build_command.add_description("Build a compressed suffix array index.");
  build_command.add_argument("dataset_name")
      .help("Path to the dataset file.");
  build_command.add_argument("sample_rate")
      .help("Sample rate for the CSA. Must be a positive integer.")
      .scan<'i', size_t>();
  build_command.add_argument("index_name")
      .help("Path to save the index file.");

  argparse::ArgumentParser query_command("query");
  query_command.add_description("Query a compressed suffix array index.");
  query_command.add_argument("index_name")
      .help("Path to the index file.");
  query_command.add_argument("num")
      .help("Number of patterns/substrings.")
      .scan<'i', size_t>();
  query_command.add_argument("len")
      .help("Length of patterns/substrings.")
      .scan<'i', size_t>();
  query_command.add_argument("oper")
      .help("Operations to perform: count, locate, extract. Can be multiple.")
      .nargs(argparse::nargs_pattern::any);

  argparse::ArgumentParser interactive_command("interactive");
  interactive_command.add_description("Load an index and run interactive queries.");
  interactive_command.add_argument("index_name")
      .help("Path to the index file.");

  program.add_subparser(build_command);
  program.add_subparser(query_command);
  program.add_subparser(interactive_command);

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    return 1;
  }

  if (program.is_subcommand_used("build")) {
    auto dataset_path = std::filesystem::path(build_command.get<std::string>("dataset_name"));
    auto sample_rate = build_command.get<size_t>("sample_rate");
    auto index_path = std::filesystem::path(build_command.get<std::string>("index_name"));

    if (!std::filesystem::exists(dataset_path)) {
      std::cerr << "Error: Dataset file not found: " << dataset_path << std::endl;
      return 1;
    }

    if (sample_rate == 0) {
      std::cerr << "Error: Invalid sample rate. Must be a positive integer." << std::endl;
      return 1;
    }

    auto benchmark = std::make_unique<BenchmarkImpl>(sample_rate);

    auto build_time = benchmark->build(dataset_path, index_path);
    std::cout << "Build time: " << build_time.count() << " seconds." << std::endl;

    size_t dataset_size = std::filesystem::file_size(dataset_path);
    size_t index_size = benchmark->space_usage_in_bytes();
    double compression_ratio = static_cast<double>(index_size) / dataset_size;
    std::cout << "Compression ratio: " << compression_ratio << std::endl;
  } else if (program.is_subcommand_used("query")) {
    auto index_path = std::filesystem::path(query_command.get<std::string>("index_name"));
    auto num = query_command.get<size_t>("num");
    auto len = query_command.get<size_t>("len");

    if (!std::filesystem::exists(index_path)) {
      std::cerr << "Error: Index file not found: " << index_path << std::endl;
      return 1;
    }

    auto benchmark = std::make_unique<BenchmarkImpl>();

    auto load_time = benchmark->load(index_path);
    std::cout << "Load time: " << load_time.count() << " ms." << std::endl;

    std::vector<std::string> opers;
    try {
      opers = query_command.get<std::vector<std::string>>("oper");
    } catch (const std::logic_error &) {
    }

    if (opers.empty()) {
      opers = {"count", "locate", "extract"};
    }

    for (const auto &op : opers) {
      if (op == "count") {
        auto count_time = benchmark->test_count(num, len);
        std::cout << "Count time per pattern: " 
                  << std::fixed << std::setprecision(3) << count_time.count() << " us." << std::endl;
      } else if (op == "locate") {
        auto locate_time = benchmark->test_locate(num, len);
        std::cout << "Locate time per pattern: " 
                  << std::fixed << std::setprecision(3) << locate_time.count() << " ms." << std::endl;
      } else if (op == "extract") {
        auto extract_time = benchmark->test_extract(num, len);
        std::cout << "Extract time per substring: "
                  << std::fixed << std::setprecision(3) << extract_time.count() << " us." << std::endl;
      } else {
        std::cerr << "Warning: Unknown operation '" << op << "', skipping." << std::endl;
      }
    }
  } else if (program.is_subcommand_used("interactive")) {
    auto index_path = std::filesystem::path(interactive_command.get<std::string>("index_name"));
    return run_interactive(index_path);
  }

  return 0;
}
