

# FastCSA

**FastCSA [1]** is a high-performance C++ library that implements a fast Compressed Suffix Array (CSA). It is designed to provide efficient full-text indexing and search capabilities while maintaining a compact memory footprint. The library leverages some modern C++20 features and AVX2/BMI2 instructions for performance optimzation.

FastCSA is the first practical compression paradigm that compresses normalized sublists [2,3] rather than the conventional $\Sigma$-list representation [4,5]. It introduces HEF, a novel code that effectively captures run distribution in normalized sublists and sublist blocks. FastCSA also adopt diverse complementary encodings to capture block-scale run characteristics and heterogeneous patterns.

For a text string T[1, t] of t symbols from an alphabet $\Sigma$ of size $\sigma$ and a pattern P[1, m] with P[i] in $\Sigma$, FastCSA supports following three fundamental queries:

*   **Count:** the number occ of occurrences of P in T;
*   **Locate:** the occ positions in T where P occurs; and
*   **Extract:** the length-l substring T[start,start + l-1] that starts at text position start.

## 1. Background

A Suffix Array (SA) is a fundamental data structure used in string processing and information retrieval. However, standard suffix arrays require significant memory (typically 8 bytes per character for large texts that length >= 4GB), which becomes impractical for massive datasets.

**fastcsa** implements a succinct data structure that compresses the suffix array (specifically using the $\Psi$ function and adaptive encoders) to reduce space usage significantly while supporting fast indexed operations. It allows users to perform the following core operations on compressed text:

*   **Count:** Quickly determine the number of occurrences of a pattern.
*   **Locate:** Find the exact positions of all occurrences of a pattern in the original text.
*   **Extract:** Decompress and retrieve an arbitrary substring from the original text without storing the original file.

The library leverages some modern C++20 features and AVX2/BMI2 instructions for optimal performance.

## 2. Getting Started

### Prerequisites

To build and use  `fastcsa`, you need the following:

*   **C++ Compiler:** Must support **C++20** (e.g., GCC 10+, Clang 10+, MSVC 19.29+).
*   **CMake:** Version 3.15 or higher.
*   **Hardware:** A CPU with **AVX2** instruction set support.
*   **Dependencies:**
    *   **libdivsufsort64**: A fast suffix array construction library. This must be installed on your system (e.g., typically built from source or installed via a package manager like `libdivsufsort-dev` on Debian/Ubuntu, though ensure it includes the 64-bit version headers).

### Building the Project

1.  Clone the repository.
2.  Create a build directory and compile using CMake:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
```

If the build is successful, you will find the `benchmark` executable in the build directory, and the library target `fastcsa` (`libfastcsa.a` in Linux) will be compiled.

### Using the Library

To use `fastcsa` library in your C++ project:

1.  Link against `fastcsa`  in your `CMakeLists.txt`.
2.  Include the header `fastcsa.hpp`.

```cpp
#include "fastcsa/fastcsa.hpp"

#include <vector>
#include <iostream>
#include <fstream>

int main() {
    // 1. Prepare data (must end with a null terminator)
    std::string text_str = "banana_bandana_panama";
    std::vector<uint8_t> data(text_str.begin(), text_str.end());
    data.push_back(0);

    // 2. Initialize CSA with a sample rate (e.g., 32)
    // Lower sample rate = faster locate/extract, but larger index size.
    // The sample rate must be a positive integer.
    fastcsa::csa index(32);

    // 3. Build the index
    std::cout << "Building index..." << std::endl;
    index.build(data);

    // 4. Usage examples
    std::string pattern_str = "ana";
    std::span<const uint8_t> pattern(reinterpret_cast<const uint8_t *>(pattern_str.data()), pattern_str.size());

    // Count
    size_t count = index.count(pattern);
    std::cout << "Pattern '" << pattern_str << "' found " << count << " times." << std::endl;

    // Locate
    std::vector<size_t> positions = index.locate(pattern);
    std::cout << "Occurrences at: ";
    for (size_t pos : positions) std::cout << pos << " ";
    std::cout << std::endl;

    // Extract text (e.g., extract 6 chars starting from index 7)
    std::vector<uint8_t> extracted = index.extract(7, 6);
    std::cout << "Extracted text: " << std::string(extracted.begin(), extracted.end()) << std::endl;

    // 5. Serialization
    std::ofstream ofs("index.bin", std::ios::binary);
    index.serialize(ofs);
  
    return 0;
}
```

### Using the Benchmark Tool

The project includes a command-line benchmark tool to measure construction speed, index size, and query performance.

#### 1. Building an Index

Use the `build` subcommand to create a compressed index from a dataset file.

```bash
# Syntax: ./benchmark build <dataset_path> <sample_rate> <output_index_path>
# Sample rate must be a positive integer and is stored in the index.

./benchmark build ./big_data.txt 64 ./data.idx
```
This command will print the **index build time** and the **compression ratio of index**.

#### 2. Querying the Index

Use the `query` subcommand to load an index and run performance tests for Count, Locate, and Extract operations.

```bash
# Syntax: ./benchmark query <index_path> <num_patterns> <pattern_len> <operations...>

# Example: Run 'count' and 'locate' tests using 1000 random patterns of length 8
./benchmark query ./data.idx 1000 8 count locate

# Example: Run 'extract' test (extracts 1000 random substrings of length 16)
./benchmark query ./data.idx 1000 16 extract
```

**Arguments:**

*   `sample_rate`: Build-only sampling interval. It must be a positive integer and is read automatically from the index during queries.
*   `num`: The number of random patterns/substrings to generate for testing.
*   `len`: The length of the patterns/substrings.
*   `oper`: Space-separated list of operations to benchmark (`count`, `locate`, `extract`).

This command will print the **index load time** and the average time per operation (in microseconds or milliseconds).


## References
[1] Hongwei Huo, Yongze Yu, and Jeffrey Scott Vitter. FastCSA: A Fast Compressed Suffix Array Using High-Order Entropy Space. https://github.com/Hongweihuo-Lab/FastCSA

[2] Roberto Grossi, Ankur Gupta, and Jeffrey Scott Vitter. 2003. High-order Entropy-compressed Text Indexes. In Proceedings of the 14th ACM-SIAM Symposium on Discrete Algorithms (SODA’03), pages 841–850. https://dl.acm.org/doi/10.5555/644108.644250

[3] Roberto Grossi, Ankur Gupta, and Jeffrey Scott Vitter. 2005. An Algorithmic Framework for Compression and Text Indexing. http://www.ittc.ku.edu/~jsv/Papers/GGV05.algorithmicframework.pdf, 38 pages.

[4] Roberto Grossi and Jeffrey Scott Vitter. 2005. Compressed Suffix Arrays and Suffix Trees with Applications to Text Indexing and String Matching. SIAM J. Comput. 35(2): 378–407. https://doi.org/10.1137/S0097539702402354

[5] Hongwei Huo, Peng Long, and Jeffrey Scott Vitter. 2023. Practical High-order Entropy-compressed Text Self-indexing. IEEE Transactions on Knowledge and Data Engineering 35(3): 2943–2960. https://doi.org/10.1109/TKDE.2021.3114401

[4] oneTBB, https://github.com/oneapi-src/oneTBB

[3] Yuta Mori, A lightweight suffix-sorting library, 2008. https://github.com/y-256/libdivsufsort/.
