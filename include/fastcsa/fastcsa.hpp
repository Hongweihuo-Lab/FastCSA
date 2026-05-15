// Copyright (C) 2026  Yongze Yu, Hongwei Huo, and Jeffrey S. Vitter
// 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
// USA

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <list>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "yaef/yaef.hpp"

namespace _Fastcsa_m_internal {
    using namespace yaef;

    using _Is = hybrid_methods::linear;
    using _Ef = hybrid_methods::eliasfano;

    [[nodiscard]] inline uint32_t _Popcnt_256(const uint64_t *_Ptr, const uint32_t _Num) {
        const uint32_t _Blockidx = _Num / 64, _Rem = _Num % 64;
        uint32_t _Res = 0;
        for (uint32_t _I = 0; _I < _Blockidx; ++_I) _Res += std::popcount(_Ptr[_I]);
        if (_Rem != 0) _Res += std::popcount(details::bits64::extract_first_bits(_Ptr[_Blockidx], _Rem));
        return _Res;
    }

    class _Bv {
    public:
        template<class _RanIt>
        struct encoder_type : hybrid_method_encoder {
            encoder_type(_RanIt _First, size_t _Num) : _Myfirst(_First), _Mynum(_Num) {
                for (size_t _I = 1; _I < _Num; ++_I) {
                    if (_First[_I] == _First[_I - 1]) { _Mybits = std::numeric_limits<size_t>::max(); return; }
                }
                const uint64_t _U = _First[_Num - 1];
                if (_U + 1 >= 256 * (64)) {
                    _Mybits = std::numeric_limits<size_t>::max();
                } else {
                    _Mybits = details::bits64::align_to<64>(_U + 1);
                }
            }
            _YAEF_ATTR_NODISCARD size_t estimated_bits() const override { return (_Mybits <= 1024) ? 0 : _Mybits; }
            _YAEF_ATTR_NODISCARD size_t required_bits() const override { return _Mybits; }
            void encode(uint8_t *_Buf, uint8_t &_Extmeta) const override {
                using details::bits64::bit_view;
                bit_view _Bv(reinterpret_cast<uint64_t *>(_Buf), bit_view::dont_care_size);
                _Bv.clear_all_bits(0, _Mybits);
                for (size_t _I = 0; _I < _Mynum; ++_I) _Bv.set_bit(_Myfirst[_I]);
                _Extmeta = details::bits64::idiv_ceil(_Myfirst[_Mynum - 1] + 1, 64) - 1;
            }
    private:
            _RanIt _Myfirst; size_t _Mynum; size_t _Mybits;
        };
        template<class _RanIt>
        static std::unique_ptr<encoder_type<_RanIt>> new_encoder(_RanIt _First, size_t _Num) {
            return std::unique_ptr<encoder_type<_RanIt>>(new encoder_type<_RanIt>(_First, _Num));
        }
        static void name(std::string *_Out) { *_Out = "BV"; }
        static void at(size_t _Offset, const uint8_t *_Buf, uint8_t _Extmeta, uint64_t *_Res) {
            const uint64_t *data64 = reinterpret_cast<const uint64_t *>(_Buf);
            *_Res = details::bits64::select_one_blocks(data64, static_cast<size_t>(_Extmeta) + 1, _Offset);
        }
        static void index_of_lower_bound(size_t _T, const uint8_t *_Buf, uint8_t _Extmeta, size_t *_Res) { 
            const uint64_t *data64 = reinterpret_cast<const uint64_t *>(_Buf);
            if (_T / 64 > static_cast<size_t>(_Extmeta) + 1) { *_Res = 256; return; }
            *_Res = details::bits64::popcount_blocks(data64, static_cast<size_t>(_Extmeta) + 1, _T);
        }
    };

    class _Re {
        static constexpr uint32_t _Block_size = details::DEFAULT_HYBRID_PARTITION_SIZE;
        static constexpr uint32_t _Num_bv_blocks = _Block_size / 64;
        static constexpr size_t _Max_num_upper_blocks = hybrid_methods::eliasfano::MAX_NUM_BLOCKS;

        struct _Rank1_last1_result {
            uint32_t _Rank = 0;
            uint32_t _Last = 0;
            uint32_t _Total = 0;
        };

        [[nodiscard]] static uint64_t _Lower_mask(uint32_t _Width) noexcept {
            return details::bits64::make_mask_lsb1(_Width);
        }

        [[nodiscard]] static uint32_t _Popcnt_all(const uint64_t *_Bv) noexcept {
            return static_cast<uint32_t>(
                std::popcount(_Bv[0]) + std::popcount(_Bv[1]) + std::popcount(_Bv[2]) + std::popcount(_Bv[3]));
        }

        [[nodiscard]] static _Rank1_last1_result _Rank1_last1(const uint64_t *_Bv, uint32_t _Idx) noexcept {
            _Rank1_last1_result _Res;
            const uint32_t _Block_idx = _Idx / 64, _Block_off = _Idx % 64;
            for (uint32_t _I = 0; _I < _Num_bv_blocks; ++_I) {
                const uint64_t _Block = _Bv[_I];
                const uint32_t _Num1 = std::popcount(_Block);
                _Res._Total += _Num1;
                if (_I < _Block_idx) {
                    _Res._Rank += _Num1;
                    if (_Block != 0) _Res._Last = 64 * _I + 63 - std::countl_zero(_Block);
                    continue;
                }
                if (_I == _Block_idx) {
                    const uint64_t _Prefix = details::bits64::extract_first_bits(_Block, _Block_off + 1);
                    _Res._Rank += std::popcount(_Prefix);
                    if (_Prefix != 0) _Res._Last = 64 * _I + 63 - std::countl_zero(_Prefix);
                }
            }
            return _Res;
        }

        [[nodiscard]] static uint32_t _Select1(const uint64_t *_Bv, uint32_t _Rank) noexcept {
            for (uint32_t _I = 0; _I < _Num_bv_blocks; ++_I) {
                const uint32_t _Num1 = std::popcount(_Bv[_I]);
                if (_Rank < _Num1) return 64 * _I + details::bits64::select_one(_Bv[_I], _Rank);
                _Rank -= _Num1;
            }
            return _Block_size;
        }

        [[nodiscard]] static const uint8_t *_Lower_addr(const uint8_t *_Buf) noexcept {
            return _Buf + _Block_size / CHAR_BIT;
        }

        [[nodiscard]] static const uint64_t *_Upper_addr(const uint8_t *_Buf, uint32_t _Num_runs, uint32_t _Low_width) noexcept {
            const size_t _Lower_bits = details::bits64::align_to<64>(static_cast<size_t>(_Num_runs) * _Low_width);
            return reinterpret_cast<const uint64_t *>(_Lower_addr(_Buf) + _Lower_bits / CHAR_BIT);
        }

        [[nodiscard]] static uint64_t _Decode_run_start(uint32_t _Run_idx, const uint8_t *_Buf, uint32_t _Low_width, uint32_t _Num_runs) noexcept {
            using details::bits64::bit_view;
            auto _Lower = bit_view(reinterpret_cast<uint64_t *>(const_cast<uint8_t *>(_Lower_addr(_Buf))), bit_view::dont_care_size);
            const uint64_t _Lo = _Lower.get_bits(static_cast<size_t>(_Run_idx) * _Low_width, _Low_width);
            const uint64_t *_Upper = _Upper_addr(_Buf, _Num_runs, _Low_width);
            const uint64_t _Hi = details::bits64::select_one_blocks(_Upper, _Max_num_upper_blocks, _Run_idx) - _Run_idx;
            if (_Low_width == 64) return _Lo;
            return (_Hi << _Low_width) | _Lo;
        }

        [[nodiscard]] static uint32_t _Run_lower_bound(uint64_t _Target, const uint8_t *_Buf, uint32_t _Low_width, uint32_t _Num_runs) noexcept {
            using details::bits64::bit_view;
            if (_Num_runs == 0 || _Target == 0) return 0;
            const uint64_t _Last = _Decode_run_start(_Num_runs - 1, _Buf, _Low_width, _Num_runs);
            if (_Target > _Last) return _Num_runs;

            const uint64_t _Hi = _Low_width == 64 ? 0 : _Target >> _Low_width;
            const uint64_t _Lo = _Low_width == 64 ? _Target : (_Target & _Lower_mask(_Low_width));
            const uint64_t *_Upper = _Upper_addr(_Buf, _Num_runs, _Low_width);

            size_t _Start = _Hi == 0 ? 0 : details::bits64::select_zero_blocks(_Upper, _Max_num_upper_blocks, _Hi - 1) - _Hi + 1;
            size_t _End = details::bits64::select_zero_blocks(_Upper, _Max_num_upper_blocks, _Hi) - _Hi;
            _Start = std::min<size_t>(_Start, _Num_runs);
            _End = std::min<size_t>(_End, _Num_runs);

            auto _Lower = bit_view(reinterpret_cast<uint64_t *>(const_cast<uint8_t *>(_Lower_addr(_Buf))), bit_view::dont_care_size);
            size_t _Base = _Start, _Len = _End - _Start;
            while (_Len > 0) {
                const size_t _Half = _Len / 2;
                const uint64_t _Val = _Lower.get_bits((_Base + _Half) * _Low_width, _Low_width);
                _Base += static_cast<size_t>(_Val < _Lo) * (_Len - _Half);
                _Len = _Half;
            }
            return static_cast<uint32_t>(_Base);
        }

        [[nodiscard]] static uint32_t _Find_run_by_value(uint64_t _Target, const uint8_t *_Buf, uint32_t _Low_width, uint32_t _Num_runs) noexcept {
            if (_Num_runs <= 1 || _Target == 0) return 0;
            const uint32_t _Lb = _Run_lower_bound(_Target, _Buf, _Low_width, _Num_runs);
            if (_Lb == _Num_runs) return _Num_runs - 1;
            const uint64_t _Val = _Decode_run_start(_Lb, _Buf, _Low_width, _Num_runs);
            if (_Val == _Target) return _Lb;
            return _Lb - 1;
        }

    public:
        template<class _RanIt>
        struct encoder_type : hybrid_method_encoder {
            encoder_type(_RanIt _First, size_t _Num) {
                if (_Num == 0) {
                    _Myestimated_bits = std::numeric_limits<size_t>::max();
                    _Myrequired_bits = std::numeric_limits<size_t>::max();
                    return;
                }
                _Myrun_starts[_Myrun_count] = _First[0];
                _Myrun_pos[_Myrun_count] = 0;
                ++_Myrun_count;
                for (size_t _I = 1; _I < _Num; ++_I) {
                    const uint64_t _Cur = _First[_I], _Prev = _First[_I - 1];
                    if (_Cur <= _Prev) {
                        _Myestimated_bits = std::numeric_limits<size_t>::max();
                        _Myrequired_bits = std::numeric_limits<size_t>::max();
                        return;
                    }
                    if (_Cur != _Prev + 1) {
                        _Myrun_starts[_Myrun_count] = _Cur;
                        _Myrun_pos[_Myrun_count] = static_cast<uint16_t>(_I);
                        ++_Myrun_count;
                    }
                }
                const uint64_t _U = _Myrun_starts[_Myrun_count - 1];
                _Mylow_width = std::max<uint32_t>(1, std::bit_width(_U / _Myrun_count));
                const size_t _Lower_bits = static_cast<size_t>(_Myrun_count) * _Mylow_width;
                const size_t _Upper_bits = static_cast<size_t>(_Myrun_count) + (_Mylow_width == 64 ? 0 : (_U >> _Mylow_width)) + 1;
                _Myestimated_bits = _Block_size + _Lower_bits + _Upper_bits;
                _Myrequired_bits = _Block_size + details::bits64::align_to<64>(_Lower_bits) + _Upper_bits;
            }
            _YAEF_ATTR_NODISCARD size_t estimated_bits() const override { return _Myestimated_bits; }
            _YAEF_ATTR_NODISCARD size_t required_bits() const override { return _Myrequired_bits; }
            void encode(uint8_t *_Buf, uint8_t &_Extmeta) const override {
                using details::bits64::bit_view;
                auto _Run_bv = bit_view(reinterpret_cast<uint64_t *>(_Buf), _Block_size);
                _Run_bv.clear_all_bits();
                for (uint32_t _I = 0; _I < _Myrun_count; ++_I) _Run_bv.set_bit(_Myrun_pos[_I]);

                _Extmeta = static_cast<uint8_t>(_Mylow_width);

                _Buf = const_cast<uint8_t *>(_Lower_addr(_Buf));
                const size_t _Lower_bits = static_cast<size_t>(_Myrun_count) * _Mylow_width;
                const size_t _Aligned_lower_bits = details::bits64::align_to<64>(_Lower_bits);
                auto _Lower = bit_view(reinterpret_cast<uint64_t *>(_Buf), _Aligned_lower_bits);
                _Lower.clear_all_bits();
                for (uint32_t _I = 0; _I < _Myrun_count; ++_I) {
                    _Lower.set_bits(static_cast<size_t>(_I) * _Mylow_width, _Mylow_width, _Myrun_starts[_I]);
                }

                _Buf += _Aligned_lower_bits / CHAR_BIT;
                const size_t _Upper_bits = _Myrequired_bits - _Block_size - _Aligned_lower_bits;
                auto _Upper = bit_view(reinterpret_cast<uint64_t *>(_Buf), details::bits64::align_to<64>(_Upper_bits));
                _Upper.clear_all_bits();
                _Upper.set_all_bits(0, _Upper_bits);

                const uint64_t _Last = _Myrun_starts[_Myrun_count - 1];
                const size_t _Num_buckets = _Mylow_width == 64 ? 0 : (_Last >> _Mylow_width);
                for (size_t _I = 0, _Run_idx = 0, _Zero_idx = 0; _I <= _Num_buckets; ++_I, ++_Zero_idx) {
                    while (_Run_idx < _Myrun_count && ((_Mylow_width == 64 ? 0 : (_Myrun_starts[_Run_idx] >> _Mylow_width)) == _I)) {
                        ++_Run_idx;
                        ++_Zero_idx;
                    }
                    _Upper.clear_bit(_Zero_idx);
                }
            }
        private:
            std::array<uint64_t, _Block_size> _Myrun_starts{};
            std::array<uint16_t, _Block_size> _Myrun_pos{};
            size_t _Myestimated_bits = 0;
            size_t _Myrequired_bits = 0;
            uint32_t _Myrun_count = 0;
            uint32_t _Mylow_width = 1;
        };
        template<class _RanIt>
        static std::unique_ptr<encoder_type<_RanIt>> new_encoder(_RanIt _First, size_t _Num) {
            return std::unique_ptr<encoder_type<_RanIt>>(new encoder_type<_RanIt>(_First, _Num));
        }
        static void name(std::string *_Out) { *_Out = "RE"; }
        static void at(size_t _Offset, const uint8_t *_Buf, uint8_t _Extmeta, uint64_t *_Res) {
            const uint64_t *_Run_bv = reinterpret_cast<const uint64_t *>(_Buf);
            if (_Offset == 0) {
                *_Res = 0;
                return;
            }
            const auto _Info = _Rank1_last1(_Run_bv, static_cast<uint32_t>(_Offset));
            const uint32_t _Run_idx = _Info._Rank - 1;
            const uint64_t _Run_start = _Decode_run_start(_Run_idx, _Buf, _Extmeta, _Info._Total);
            *_Res = _Run_start + (_Offset - _Info._Last);
        }
        static void index_of_lower_bound(size_t _T, const uint8_t *_Buf, uint8_t _Extmeta, size_t *_Res) {
            const uint64_t *_Run_bv = reinterpret_cast<const uint64_t *>(_Buf);
            if (_T == 0) {
                *_Res = 0;
                return;
            }
            const uint32_t _Num_runs = _Popcnt_all(_Run_bv);
            const uint32_t _Run_idx = _Find_run_by_value(_T, _Buf, _Extmeta, _Num_runs);
            const uint64_t _Run_start = _Decode_run_start(_Run_idx, _Buf, _Extmeta, _Num_runs);
            const uint32_t _Run_pos = _Select1(_Run_bv, _Run_idx);
            const uint32_t _Next_run_pos = _Run_idx + 1 < _Num_runs ? _Select1(_Run_bv, _Run_idx + 1) : _Block_size;
            const uint64_t _Cand = _Run_pos + (_T - _Run_start);
            *_Res = std::min<uint64_t>(_Cand, _Next_run_pos);
        }
    };

    class _Bg {
    public:
        template<class _RanIt>
        struct encoder_type : hybrid_method_encoder {
            encoder_type(_RanIt _First, size_t _Num) : _Myfirst(_First), _Mynum(_Num) {
                for (size_t _I = 1; _I < _Num; ++_I) {
                    uint64_t _G = _First[_I] - _First[_I - 1];
                    if (_G == 1) continue;
                    --_G;
                    uint32_t _W = std::max<uint32_t>(1, std::bit_width(_G));
                    _Mybits += 2 * _W - 1;
                    _Myunarybits += _W;
                    _Mybodybits += _W - 1;
                }
                _Mybits += _Num;
                _Myunarybits = details::bits64::align_to<64>(_Myunarybits);
                _Mybodybits = details::bits64::align_to<64>(_Mybodybits);
            }
            _YAEF_ATTR_NODISCARD size_t estimated_bits() const override { return _Mybits; }
            _YAEF_ATTR_NODISCARD size_t required_bits() const override { return _Myunarybits + _Mybodybits + 256; }
            void encode(uint8_t *_Buf, uint8_t &_Extmeta) const override {
                using details::bits64::bit_view;
                const size_t _Num_unary = _Myunarybits / 64;
                _Extmeta = static_cast<uint8_t>(_Num_unary - 1);
                {
                    auto _Bv = bit_view(reinterpret_cast<uint64_t *>(_Buf), 256);
                    _Bv.clear_all_bits();
                    for (size_t _I = 1; _I < _Mynum; ++_I) {
                      uint64_t _G = _Myfirst[_I] - _Myfirst[_I - 1];
                      if (_G != 1) _Bv.set_bit(_I - 1);
                    }
                }
                _Buf += 256 / CHAR_BIT;
                auto _Unary = bit_view(reinterpret_cast<uint64_t *>(_Buf), _Myunarybits);
                auto _Body = bit_view(reinterpret_cast<uint64_t *>(_Buf) + _Num_unary, _Mybodybits);
                _Unary.clear_all_bits();
                _Body.clear_all_bits();
                for (size_t _I = 1, _Ubits = 0, _Bbits = 0; _I < _Mynum; ++_I) {
                    uint64_t _G = _Myfirst[_I] - _Myfirst[_I - 1];
                    if (_G == 1) continue;
                    --_G;
                    uint32_t _W = std::bit_width(_G);
                    _Unary.set_bit(_Ubits + _W - 1);
                    _Ubits += _W;
                    if (_W > 1) _Body.set_bits(_Bbits, _W - 1, _G);
                    _Bbits += _W - 1;
                }
            }
        private:
            _RanIt _Myfirst; size_t _Mynum; size_t _Mybits = 0; size_t _Myunarybits = 0; size_t _Mybodybits = 0;
        };
        template<class _RanIt>
        static std::unique_ptr<encoder_type<_RanIt>> new_encoder(_RanIt _First, size_t _Num) {
            return std::unique_ptr<encoder_type<_RanIt>>(new encoder_type<_RanIt>(_First, _Num));
        }
        static void name(std::string *_Out) { *_Out = "BG"; }
        static void at(size_t _Offset, const uint8_t *_Buf, uint8_t _Extmeta, uint64_t *_Res) {
            using details::bits64::bit_view;
            const uint64_t *_Exception = reinterpret_cast<const uint64_t *>(_Buf);
            uint32_t _Num_exception = _Popcnt_256(_Exception, _Offset);
            uint64_t _Resval = _Offset - _Num_exception;
            _Buf += 256 / CHAR_BIT;
            auto _Unarydata = reinterpret_cast<uint64_t *>(const_cast<uint8_t *>(_Buf));
            const size_t _Num_unary = static_cast<size_t>(_Extmeta) + 1;
            auto _Body = bit_view(reinterpret_cast<uint64_t *>(const_cast<uint8_t *>(_Buf)) + _Num_unary, bit_view::dont_care_size);
            auto _Bodyreader = _Body.new_reader();
            size_t _Prvpos = -1;
            for (size_t _I = 0, _J = 0; _J < _Num_exception && _I < _Num_unary; ++_I) {
                uint64_t _Block = _Unarydata[_I];
                while (_J < _Num_exception && _Block) {
                    uint64_t _Tmp = _Block & -_Block;
                    size_t _Pos1 = 64 * _I + std::countr_zero(_Block);
                    uint32_t _W = _Pos1 - _Prvpos - 1;
                    uint64_t _B = _Bodyreader.read_bits(_W);
                    uint64_t _G = details::bits64::set_bit(_B, _W) + 1;
                    _Resval += _G;
                    ++_J;
                    _Block ^= _Tmp;
                    _Prvpos = _Pos1;
                }
            }
            *_Res = _Resval;
        }
        static void index_of_lower_bound(size_t _T, const uint8_t *_Buf, uint8_t _Extmeta, size_t *_Res) {
            using details::bits64::bit_view;
            const uint64_t *_Exception = reinterpret_cast<const uint64_t *>(_Buf);
            _Buf += 256 / CHAR_BIT;
            auto _Unarydata = reinterpret_cast<uint64_t *>(const_cast<uint8_t *>(_Buf));
            const size_t _Num_unary = static_cast<size_t>(_Extmeta) + 1;
            _Buf += _Num_unary * sizeof(uint64_t);
            auto _Body = bit_view(reinterpret_cast<uint64_t *>(const_cast<uint8_t *>(_Buf)), bit_view::dont_care_size);
            auto _Bodyreader = _Body.new_reader();
            uint64_t _Unaryblock = _Unarydata[0];
            uint8_t _Unaryidx = 0;
            uint32_t _Prvpos = -1;
            auto _Decodeunary = [&]() -> uint32_t {
                while (_Unaryblock == 0) {
                    ++_Unaryidx;
                    _Unaryblock = _Unarydata[_Unaryidx];
                }
                uint32_t _Pos1 = 64 * _Unaryidx + std::countr_zero(_Unaryblock);
                _Unaryblock = _blsr_u64(_Unaryblock);
                uint32_t _W = _Pos1 - _Prvpos - 1;
                _Prvpos = _Pos1;
                return _W;
            };
            uint64_t _Val = 0;
            size_t _Resval = 0;
            for (size_t _I = 0; _I < 4; ++_I) {
                uint64_t _Block = _Exception[_I];
                uint32_t _Blockbits = 64;
                while (_Block) {
                    uint32_t _Numgap1 = std::countr_zero(_Block);
                    _Block >>= _Numgap1;
                    _Blockbits -= _Numgap1;
                    uint32_t _Step = std::min<uint64_t>(_Numgap1, _T - _Val);
                    _Val += _Step;
                    _Resval += _Step;
                    if (_Val == _T) { *_Res = _Resval; return; }
                    uint32_t _Num_exception = std::countr_one(_Block);
                    _Block = _Num_exception == 64 ? 0 : _Block >> _Num_exception;
                    _Blockbits -= _Num_exception;
                    for (uint32_t _J = 0; _J < _Num_exception; ++_J) {
                        uint32_t _W = _Decodeunary();
                        uint64_t _B = _Bodyreader.read_bits(_W);
                        uint64_t _G = details::bits64::set_bit(_B, _W) + 1;
                        _Val += _G;
                        ++_Resval;
                        if (_Val >= _T) { *_Res = _Resval; return; }
                    }
                }
                if (_Blockbits != 0) {
                    uint32_t _Step = std::min<uint64_t>(_Blockbits, _T - _Val);
                    _Val += _Step;
                    _Resval += _Step;
                    if (_Val == _T) { *_Res = _Resval; return; }
                }
            }
            _Resval += std::min<size_t>(_T - _Val, 256 - _Resval);
            *_Res = _Resval;
        }
    };
}

namespace _Fastcsa_internal {

    using _I8 = int8_t;
    using _I16 = int16_t;
    using _I32 = int32_t;
    using _I64 = int64_t;

    using _U8 = uint8_t;
    using _U16 = uint16_t;
    using _U32 = uint32_t;
    using _U64 = uint64_t;

    using _F32 = float;
    using _F64 = double;

    template <class _Ty>
    struct _Interval {
        using value_type = _Ty;
        value_type _Min{std::numeric_limits<value_type>::max()};
        value_type _Max{std::numeric_limits<value_type>::max()};
        static _Interval make_empty() noexcept { return _Interval{std::numeric_limits<value_type>::max(), std::numeric_limits<value_type>::max()}; }
        [[nodiscard]] bool empty() const noexcept { return _Min >= _Max; }
        [[nodiscard]] value_type length() const noexcept { return empty() ? 0 : _Max - _Min; }
        [[nodiscard]] bool contains(value_type _Val) const noexcept { return _Val >= _Min && _Val < _Max; }
        template <class _Uty>
        [[nodiscard]] friend bool operator==(const _Interval<_Uty>& _Lhs, const _Interval<_Uty>& _Rhs) noexcept { return _Lhs._Min == _Rhs._Min && _Lhs._Max == _Rhs._Max; }
    };

    template <_U32 _Nx>
    using _Chars = std::array<_U8, _Nx>;

    template <class _Container_Ty>
        requires requires(const _Container_Ty& _Ctn) { { _Ctn.space_usage_in_bytes() } -> std::convertible_to<size_t>; }
    [[nodiscard]] inline size_t _Space_usage_in_bytes(const _Container_Ty& _Cont) { return _Cont.space_usage_in_bytes(); }

    template <class _Ty>
        requires(std::is_trivial_v<_Ty>&& std::is_trivially_copyable_v<_Ty>)
    [[nodiscard]] inline size_t _Space_usage_in_bytes(const std::vector<_Ty>& _Vec) { return sizeof(_Ty) * _Vec.size(); }

    template <class _Ty, size_t _Num>
        requires(std::is_trivial_v<_Ty>&& std::is_trivially_copyable_v<_Ty>)
    [[nodiscard]] inline size_t _Space_usage_in_bytes(const std::array<_Ty, _Num>&) { return sizeof(_Ty) * _Num; }

    [[nodiscard]] inline _U32 _Safe_bit_width(_U64 _Val) noexcept { return _Val == 0 ? 1 : std::bit_width(_Val); }

    template <class _Ty>
        requires std::is_enum_v<_Ty>
    [[nodiscard]] inline constexpr auto _To_underlying(_Ty _Val) noexcept { return static_cast<std::underlying_type_t<_Ty>>(_Val); }

    [[nodiscard]] inline _U64 _Integral_div_ceil(_U64 _Lhs, _U64 _Rhs) noexcept { return yaef::details::bits64::idiv_ceil(_Lhs, _Rhs); }

    [[noreturn]] inline void _Mark_as_unreachable() noexcept { __builtin_unreachable(); }

    template <class _RanIt, class _Size, class _Ty, class _Pr = std::identity>
    [[nodiscard]] inline _RanIt _Lower_bound(_RanIt _First, _Size _Count, _Ty _Val, const _Pr& _Proj = _Pr()) {
        _RanIt _Base = _First;
        _Size _Len = _Count;
        while (_Len > 0) {
            _Size _Half = _Len / 2;
            _Base += (_Proj(_Base[_Half]) < _Val) * (_Len - _Half);
            _Len = _Half;
        }
        return _Base;
    }

    template <class _RanIt, class _Size, class _Ty, class _Pr = std::identity>
    [[nodiscard]] inline _RanIt _Upper_bound(_RanIt _First, _Size _Count, _Ty _Val, const _Pr& _Proj = _Pr()) {
        _RanIt _Base = _First;
        _Size _Len = _Count;
        while (_Len > 0) {
            _Size _Half = _Len / 2;
            _Base += (_Proj(_Base[_Half]) <= _Val) * (_Len - _Half);
            _Len = _Half;
        }
        return _Base;
    }

    [[nodiscard]] inline double _Calculate_ratio(double _Lhs, double _Rhs) noexcept { return _Lhs / _Rhs; }
    [[nodiscard]] inline double _Calculate_percent(double _Lhs, double _Rhs) noexcept { return _Lhs / _Rhs * 100.0; }

    class _B : public yaef::bit_buffer<> {
        using _Base = yaef::bit_buffer<>;
    public:
        using _Base::_Base;
        _B() = default;
        _B(const _B&) = default;
        _B(_B&&) = default;
        explicit _B(size_type _Size) : _Base(_Size) {}
        _B& operator=(const _B&) = default;
        _B& operator=(_B&&) = default;
        void serialize(std::ostream& _Out) const {
            yaef::serialize_to_stream(static_cast<const _Base&>(*this), _Out);
        }
        void deserialize(std::istream& _In) {
            yaef::deserialize_from_stream(static_cast<_Base&>(*this), _In);
        }
    };

    class _P : public yaef::packed_int_buffer<> {
        using _Base = yaef::packed_int_buffer<>;
    public:
        _P() = default;
        _P(const _P&) = default;
        _P(_P&&) = default;
        _P(_U32 _Width, size_type _Size) : _Base(_Width, _Size) {}
        _P& operator=(const _P&) = default;
        _P& operator=(_P&&) = default;
        void serialize(std::ostream& _Out) const {
            yaef::serialize_to_stream(static_cast<const _Base&>(*this), _Out);
        }
        void deserialize(std::istream& _In) {
            yaef::deserialize_from_stream(static_cast<_Base&>(*this), _In);
        }
    };

    template <_U32 _Width>
    class _Wp : public yaef::wpacked_int_buffer<_Width> {
        using _Base = yaef::wpacked_int_buffer<_Width>;
    public:
        using typename _Base::size_type;
        _Wp() = default;
        _Wp(const _Wp&) = default;
        _Wp(_Wp&&) = default;
        explicit _Wp(size_type _Size) : _Base(_Size) {}
        _Wp& operator=(const _Wp&) = default;
        _Wp& operator=(_Wp&&) = default;
        void serialize(std::ostream& _Out) const {
            yaef::serialize_to_stream(static_cast<const _Base&>(*this), _Out) ;
        }
        void deserialize(std::istream& _In) {
            yaef::deserialize_from_stream(static_cast<_Base&>(*this), _In);
        }
    };

    class _B_s1 : public yaef::eliasfano_sparse_bitmap<true> {
    public:
        using _Base = yaef::eliasfano_sparse_bitmap<true>;
        _B_s1() = default;
        _B_s1(const _B_s1&) = default;
        _B_s1(_B_s1&&) = default;
        explicit _B_s1(const _B& _Bits) : _Base(_Bits.block_data(), _Bits.size()) {}
        _B_s1(const _B& _Bits, size_type _Num_ones) : _Base(_Bits.block_data(), _Bits.size(), _Num_ones) {}
        template <std::random_access_iterator _RanIt>
        _B_s1(size_t _Num_bits, _RanIt _First, _RanIt _Last) : _Base(_Num_bits, _First, _Last) {}
        _B_s1& operator=(const _B_s1&) = default;
        _B_s1& operator=(_B_s1&&) = default;
        void serialize(std::ostream& _Out) const {
            yaef::serialize_to_stream(static_cast<const _Base&>(*this), _Out);
        }
        void deserialize(std::istream& _In) {
            yaef::deserialize_from_stream(static_cast<_Base&>(*this), _In);
        }
    };

    class _Ch {
    public:
        using size_type = size_t;
        using symbol_type = _U8;
        static constexpr symbol_type min_value = 0;
        static constexpr symbol_type max_value = 255;
        static constexpr size_type max_size = 256;

        _Ch() : _Mysize(0) {}
        _Ch(const _Ch&) = default;
        _Ch(_Ch&& _Other) noexcept
            : _Myflags(std::move(_Other._Myflags)), _Mycounters(std::move(_Other._Mycounters)), _Mymap(std::move(_Other._Mymap)), _Myunmap(std::move(_Other._Myunmap)), _Mysize(std::exchange(_Other._Mysize, 0)) {}
        explicit _Ch(std::span<const symbol_type> _Text);
        _Ch& operator=(const _Ch&) = default;
        _Ch& operator=(_Ch&& _Other) noexcept {
            _Myflags = std::move(_Other._Myflags); _Mycounters = std::move(_Other._Mycounters); _Mymap = std::move(_Other._Mymap); _Myunmap = std::move(_Other._Myunmap); _Mysize = std::exchange(_Other._Mysize, 0); return *this;
        }
        [[nodiscard]] size_type space_usage_in_bytes() const { return _Myflags.space_usage_in_bytes() + _Mycounters.size() * sizeof(size_type) + _Mymap.size() * sizeof(symbol_type) + _Myunmap.size() * sizeof(symbol_type); }
        [[nodiscard]] size_type size() const { return _Mysize; }
        [[nodiscard]] bool empty() const { return size() == 0; }
        [[nodiscard]] bool is_mapped(symbol_type _Sym) const { return _Myflags.get_bit(_Sym); }
        [[nodiscard]] bool contains(symbol_type _Sym) const { return _Sym < size(); }
        [[nodiscard]] size_type counter(size_type _Sym) const { return _Mycounters[_Sym]; }
        [[nodiscard]] std::span<const size_type> counters() const { return _Mycounters; }
        [[nodiscard]] symbol_type search_symbol(size_type _Rank) const {
            auto _It = _Upper_bound(_Mycounters.begin(), _Mycounters.size(), _Rank);
            return unmap(static_cast<symbol_type>(std::distance(_Mycounters.begin(), _It) - 1));
        }
        [[nodiscard]] symbol_type search_symbol_mapped(size_type _Rank) const {
            auto _It = _Upper_bound(_Mycounters.begin(), _Mycounters.size(), _Rank);
            return static_cast<symbol_type>(std::distance(_Mycounters.begin(), _It) - 1);
        }
        [[nodiscard]] size_type freq(symbol_type _Sym) const {
            auto _Idx = static_cast<size_type>(_Sym); return _Mycounters[_Idx + 1] - _Mycounters[_Idx];
        }
        [[nodiscard]] symbol_type map(symbol_type _Sym) const { return _Mymap[_Sym]; }
        [[nodiscard]] symbol_type unmap(symbol_type _Sym) const { return _Myunmap[_Sym]; }
        void swap(_Ch& _Other) noexcept {
            _Myflags.swap(_Other._Myflags); _Mycounters.swap(_Other._Mycounters); _Mymap.swap(_Other._Mymap); _Myunmap.swap(_Other._Myunmap); std::swap(_Mysize, _Other._Mysize);
        }
        void serialize(std::ostream& _Out) const;
        void deserialize(std::istream& _In);
    private:
        _B _Myflags;
        std::vector<size_type> _Mycounters;
        std::vector<symbol_type> _Mymap;
        std::vector<symbol_type> _Myunmap;
        size_type _Mysize;
    };

    template <class _Derived, _U32 _Nx, bool _Countable>
    class _Ctxb {
    public:
        using chars_type = _Chars<_Nx>;
        _Ctxb() = default;
        explicit _Ctxb(std::vector<size_t> _Counters) : _Mycounters(std::move(_Counters)) { _Init_exclusive_scan(); }
        [[nodiscard]] size_t size() const noexcept { return _Mycounters.size() - 1; }
        [[nodiscard]] const std::vector<size_t>& counters() const noexcept { return _Mycounters; }
        [[nodiscard]] size_t freq(_U64 _Idx) const noexcept { return _Mycounters[_Idx + 1] - _Mycounters[_Idx]; }
        [[nodiscard]] size_t freq(const chars_type& _Ch) const noexcept { return freq(static_cast<const _Derived&>(*this).map(_Ch)); }
        [[nodiscard]] size_t excl_counter(_U64 _Idx) const noexcept { return _Mycounters[_Idx]; }
        [[nodiscard]] size_t excl_counter(const chars_type& _Ch) const noexcept { return excl_counter(static_cast<const _Derived&>(*this).map(_Ch)); }
        [[nodiscard]] size_t incl_counter(_U64 _Idx) const noexcept { return _Mycounters[_Idx + 1]; }
        [[nodiscard]] size_t incl_counter(const chars_type& _Ch) const noexcept { return incl_counter(static_cast<const _Derived&>(*this).map(_Ch)); }
        void serialize(std::ostream& _Out) const {
            size_t _Num = _Mycounters.size();
            _Out.write(reinterpret_cast<const char*>(&_Num), sizeof(_Num));
            _Out.write(reinterpret_cast<const char*>(_Mycounters.data()), sizeof(size_t) * _Mycounters.size());
            static_cast<const _Derived&>(*this)._Do_serialize(_Out);
        }
        void deserialize(std::istream& _In) {
            size_t _Num;
            _In.read(reinterpret_cast<char*>(&_Num), sizeof(_Num));
            _Mycounters.resize(_Num);
            _In.read(reinterpret_cast<char*>(_Mycounters.data()), sizeof(size_t) * _Num);
            static_cast<_Derived&>(*this)._Do_deserialize(_In);
        }
    protected:
        std::vector<size_t> _Mycounters;
        [[nodiscard]] size_t _Base_space_usage_in_bytes() const noexcept { return _Fastcsa_internal::_Space_usage_in_bytes(_Mycounters); }
        void _Base_swap(_Ctxb& _Other) noexcept { _Mycounters.swap(_Other._Mycounters); }
    private:
        void _Init_exclusive_scan() {
            _Mycounters.resize(_Mycounters.size() + 1);
            size_t _Acc = 0;
            for (size_t _I = 0; _I < _Mycounters.size(); ++_I) { size_t _Val = _Acc; _Acc += _Mycounters[_I]; _Mycounters[_I] = _Val; }
        }
    };

    template <class _Derived, _U32 _Nx>
    class _Ctxb<_Derived, _Nx, false> {
    public:
        using chars_type = _Chars<_Nx>;
        void serialize(std::ostream& _Out) const { static_cast<const _Derived&>(*this)._Do_serialize(_Out); }
        void deserialize(std::istream& _In) { static_cast<_Derived&>(*this)._Do_deserialize(_In); }
    protected:
        [[nodiscard]] size_t _Base_space_usage_in_bytes() const noexcept { return 0; }
        void _Base_swap(_Ctxb&) noexcept {}
    };
    

    template <_U32 _Nx, bool _Countable = true> class _Ctx;

    template <bool _Countable>
    class _Ctx<1, _Countable> : public _Ctxb<_Ctx<1, _Countable>, 1, _Countable> {
        using _Base = _Ctxb<_Ctx<1, _Countable>, 1, _Countable>;
        friend class _Ctx::_Ctxb;
    public:
        using typename _Base::chars_type;
        _Ctx() = default;
        _Ctx(const std::vector<chars_type>& _Chars, std::vector<size_t> _Counters) : _Base(std::move(_Counters)) {
            _Myfwd.fill(std::numeric_limits<_U64>::max()); _Mybwd.resize(_Chars.size());
            for (_U64 _I = 0; _I < _Chars.size(); ++_I) { auto _Val = _Map_chars_to_index(_Chars[_I]); _Myfwd[_Val] = _I; _Mybwd[_I] = _Val; }
        }
        [[nodiscard]] size_t space_usage_in_bytes() const noexcept { return _Fastcsa_internal::_Space_usage_in_bytes(_Myfwd) + _Fastcsa_internal::_Space_usage_in_bytes(_Mybwd) + this->_Base_space_usage_in_bytes(); }
        [[nodiscard]] bool contains(chars_type _Ch) const noexcept { return _Myfwd[_Map_chars_to_index(_Ch)] != std::numeric_limits<_U64>::max(); }
        [[nodiscard]] _U64 map(chars_type _Ch) const noexcept { return _Myfwd[_Map_chars_to_index(_Ch)]; }
        [[nodiscard]] chars_type unmap(_U64 _Idx) const noexcept { return chars_type{static_cast<_U8>(_Mybwd[_Idx])}; }
        void swap(_Ctx& _Other) noexcept { _Myfwd.swap(_Other._Myfwd); _Mybwd.swap(_Other._Mybwd); this->_Base_swap(_Other); }
    private:
        std::array<_U64, 256> _Myfwd;
        std::vector<char> _Mybwd;
        void _Do_serialize(std::ostream& _Out) const {
            _Out.write(reinterpret_cast<const char*>(_Myfwd.data()), sizeof(_U64) * _Myfwd.size());
            size_t _Num = _Mybwd.size();
            _Out.write(reinterpret_cast<const char*>(&_Num), sizeof(_Num));
            _Out.write(reinterpret_cast<const char*>(_Mybwd.data()), sizeof(char) * _Mybwd.size());
        }
        void _Do_deserialize(std::istream& _In) {
            _In.read(reinterpret_cast<char*>(_Myfwd.data()), sizeof(_U64) * _Myfwd.size());
            size_t _Num; _In.read(reinterpret_cast<char*>(&_Num), sizeof(_Num));
            _Mybwd.resize(_Num); _In.read(reinterpret_cast<char*>(_Mybwd.data()), sizeof(char) * _Num);
        }
        static _U64 _Map_chars_to_index(chars_type _Ch) noexcept { return _Ch[0]; }
    };

    void _Construct_suffix_array(std::span<const _U8> _Text, std::span<_U64> _Sa);
    void _Construct_psi(const _Ch& _Alphabet, std::span<const _U8> _Text, std::span<const _U64> _Sa, std::span<_U64> _Psi);

    template <class _D>
    class _Psib {
    public:
        using size_type = size_t;
        using value_type = _U64;
        class _Ctxi {
            using _Storage_type = yaef::eliasfano_list<_U64>;
        public:
            _Ctxi() = default;
            template <std::random_access_iterator _RanIt> _Ctxi([[maybe_unused]] size_type _Len, _RanIt _First, _RanIt _Last) : _Mymarks(yaef::from_sorted, _First, _Last) {}
            [[nodiscard]] size_type num_contexts() const noexcept { return _Mymarks.size() - 1; }
            [[nodiscard]] size_type index_of(size_type _Idx) const noexcept { return _Mymarks.index_of_lower_bound(_Idx + 1) - 1; }
            [[nodiscard]] _Interval<size_type> interval_of(size_type _Ctx_idx) const noexcept { return _Interval<size_type>{_Mymarks[_Ctx_idx], _Mymarks[_Ctx_idx + 1]}; }
            [[nodiscard]] size_type space_usage_in_bytes() const noexcept { return _Mymarks.space_usage_in_bytes(); }
            void swap(_Ctxi& _Other) noexcept { _Mymarks.swap(_Other._Mymarks); }
            void serialize(std::ostream& _Out) const { yaef::serialize_to_stream(_Mymarks, _Out); }
            void deserialize(std::istream& _In) { yaef::deserialize_from_stream(_Mymarks, _In); }
        private:
            _Storage_type _Mymarks;
        };
        _Psib() = default;
        _Psib(const _Psib&) = delete;
        _Psib(_Psib&&) noexcept = default;
        _Psib& operator=(const _Psib&) = delete;
        _Psib& operator=(_Psib&&) noexcept = default;
        
        void build(const _Ch& _Alphabet, std::span<const _U8> _Text, std::span<_U64> _Psi) {
            _Mysize = _Text.size();
            {
                std::vector<size_type> _Counters(_Alphabet.size());
                for (size_t _I = 0; _I < _Alphabet.size(); ++_I) _Counters[_I] = _Alphabet.freq(_I);
                std::vector<_Chars<1>> _Ctxs(_Alphabet.size());
                for (size_t _I = 0; _I < _Alphabet.size(); ++_I) _Ctxs[_I][0] = _I;
                _Myctx_table = _Ctx<1>(_Ctxs, std::move(_Counters));
                _Myctx_indexer = _Ctxi(_Text.size(), _Alphabet.counters().begin(), _Alphabet.counters().end());
            }
            {
                std::vector<size_type> _Subctx_starts;
                std::vector<size_type> _Unpacked_subctx_list;
                std::vector<std::vector<size_type>> _Unpacked_subctx_lists(_Myctx_indexer.num_contexts());
                std::vector<std::vector<size_type>> _Subctx_starts_lists(_Myctx_indexer.num_contexts());
                for (size_type _I = 1; _I <= _Myctx_indexer.num_contexts(); ++_I) {
                    auto _Interval = _Myctx_indexer.interval_of(_I - 1);
                    size_type _L = _Interval._Min, _R = _Interval._Max;
                    for (size_type _J = _L; _J < _R; ) {
                        _U8 _Subctx_char = _Alphabet.search_symbol_mapped(_Psi[_J]);
                        _Chars<1> _Subctx{_Subctx_char};
                        _Subctx_starts_lists[_I - 1].push_back(_J);
                        _Unpacked_subctx_lists[_I - 1].push_back(_Myctx_table.map(_Subctx));
                        auto _Iter = _Upper_bound(_Psi.data() + _J + 1, _R - _J - 1, _Subctx_char,
                            [&](size_type _Idx) { return _Alphabet.search_symbol_mapped(_Idx); });
                        _J = std::distance(_Psi.data(), _Iter);
                    }
                }
                for (auto& _List : _Subctx_starts_lists) _Subctx_starts.insert(_Subctx_starts.end(), _List.begin(), _List.end());
                _Subctx_starts.push_back(_Text.size());
                for (auto& _List : _Unpacked_subctx_lists) _Unpacked_subctx_list.insert(_Unpacked_subctx_list.end(), _List.begin(), _List.end());
                _Chars<1> _Subctx{_Text[1]};
                _Unpacked_subctx_list.push_back(_Myctx_table.map(_Subctx));
                _Mysubctx_list = _P{_Safe_bit_width(_Myctx_table.size()), _Unpacked_subctx_list.size()};
                for (size_type _I = 0; _I < _Unpacked_subctx_list.size(); ++_I) _Mysubctx_list.set_value(_I, _Unpacked_subctx_list[_I]);
                _Mysubctx_indexer = _Ctxi(_Text.size(), _Subctx_starts.begin(), _Subctx_starts.end());
            }
            for (size_type _I = 0; _I < _Text.size(); ++_I) {
                size_type _Subctx_idx = _Mysubctx_indexer.index_of(_I);
                size_type _Subctx = _Mysubctx_list.get_value(_Subctx_idx);
                _Psi[_I] -= _Myctx_table.excl_counter(_Subctx);
            }
            _To_derived()._Do_build(_Psi);
        }

        [[nodiscard]] size_type size() const noexcept { return _Mysize; }
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }
        [[nodiscard]] size_type num_contexts() const noexcept { return _Myctx_table.size(); }
        [[nodiscard]] size_type num_subcontexts() const noexcept { return _Mysubctx_list.size(); }
        [[nodiscard]] size_type space_usage_in_bytes() const noexcept { return _Myctx_table.space_usage_in_bytes() + _Myctx_indexer.space_usage_in_bytes() + _Mysubctx_indexer.space_usage_in_bytes() + _Mysubctx_list.space_usage_in_bytes() + _To_derived()._Do_space_usage_in_bytes(); }
        [[nodiscard]] const _Ctxi& context_indexer() const noexcept { return _Myctx_indexer; }
        [[nodiscard]] const _Ctxi& subcontext_indexer() const noexcept { return _Mysubctx_indexer; }
        [[nodiscard]] const _Ctx<1>& context_table() const noexcept { return _Myctx_table; }
        [[nodiscard]] _Ctx<1>& context_table() noexcept { return _Myctx_table; }
        [[nodiscard]] size_type size_of_context(size_type _Index) const noexcept { return _Myctx_indexer.interval_of(_Index).length(); }
        [[nodiscard]] size_type size_of_subcontext(size_type _Index) const noexcept { return _Mysubctx_indexer.interval_of(_Index).length(); }
        [[nodiscard]] value_type at(size_type _Index) const {
            size_type _Subctx_idx = _Mysubctx_indexer.index_of(_Index);
            auto _Subctx_interval = _Mysubctx_indexer.interval_of(_Subctx_idx);
            value_type _Reduced = _Myctx_table.excl_counter(_Mysubctx_list[_Subctx_idx]);
            value_type _Small_psi = _To_derived()._Do_get_psi(_Index, _Subctx_idx, _Subctx_interval);
            return _Small_psi + _Reduced;
        }
        [[nodiscard]] value_type operator[](size_type _Index) const { return at(_Index); }
        [[nodiscard]] size_type lower_bound(_Interval<size_type> _Ctx_interval, value_type _Target) const {
            size_type _First_subctx_idx = _Mysubctx_indexer.index_of(_Ctx_interval._Min);
            size_type _Last_subctx_idx = _Mysubctx_indexer.index_of(_Ctx_interval._Max - 1);
            size_type _Len = _Last_subctx_idx - _First_subctx_idx + 1;
            size_type _Base = _First_subctx_idx;
            while (_Len > 0) {
                size_type _Half = _Len / 2;
                size_type _Mid = _Base + _Half;
                if (_Myctx_table.excl_counter(_Mysubctx_list[_Mid]) < _Target) {
                    _Base = _Mid + 1;
                    _Len = _Len - _Half - 1;
                } else {
                    _Len = _Half;
                }
            }
            auto _Subctx_idx = _Base;
            if (_Target != _Myctx_table.excl_counter(_Mysubctx_list[_Subctx_idx])) --_Subctx_idx;
            value_type _Reduced = _Myctx_table.excl_counter(_Mysubctx_list[_Subctx_idx]);
            auto _Subctx_interval = _Mysubctx_indexer.interval_of(_Subctx_idx);
            return _To_derived()._Do_lower_bound(_Subctx_idx, _Subctx_interval, _Target - _Reduced);
        }
        void swap(_D& _Other) noexcept { std::swap(_Mysize, _Other._Mysize); _Myctx_table.swap(_Other._Myctx_table); _Myctx_indexer.swap(_Other._Myctx_indexer); _Mysubctx_indexer.swap(_Other._Mysubctx_indexer); _Mysubctx_list.swap(_Other._Mysubctx_list); _To_derived()._Do_swap(_Other); }
        void serialize(std::ostream& _Out) const { _Out.write(reinterpret_cast<const char*>(&_Mysize), sizeof(_Mysize)); _Myctx_table.serialize(_Out); _Myctx_indexer.serialize(_Out); _Mysubctx_indexer.serialize(_Out); _Mysubctx_list.serialize(_Out); _To_derived()._Do_serialize(_Out); }
        void deserialize(std::istream& _In) { _In.read(reinterpret_cast<char*>(&_Mysize), sizeof(_Mysize)); _Myctx_table.deserialize(_In); _Myctx_indexer.deserialize(_In); _Mysubctx_indexer.deserialize(_In); _Mysubctx_list.deserialize(_In); _To_derived()._Do_deserialize(_In); }
    protected:
        size_type _Mysize{0};
        _Ctx<1> _Myctx_table;
        _Ctxi _Myctx_indexer;
        _Ctxi _Mysubctx_indexer;
        _P _Mysubctx_list;
    private:
        [[nodiscard]] const _D& _To_derived() const noexcept { return static_cast<const _D&>(*this); }
        [[nodiscard]] _D& _To_derived() noexcept { return static_cast<_D&>(*this); }
    };

    template <class _P_Ty>
    class _Sab {
        struct _Dummy_sa_callback { void operator()(std::span<size_t>) const {} };
    public:
        using size_type = size_t;
        using alphabet_type = _Ch;
        using psi_array_type = _P_Ty;
        using sa_interval_type = _Interval<size_t>;
        _Sab() = default;
        explicit _Sab(size_type _Sa_sample_rate) : _Mysa_sample_rate(_Sa_sample_rate) { _Validate_sample_rate(_Mysa_sample_rate); }
        _Sab(const _Sab&) = delete;
        _Sab(_Sab&& _Other) noexcept : _Mysa_sample_rate(_Other._Mysa_sample_rate), _Myalphabet(std::move(_Other._Myalphabet)), _Mypsi(std::move(_Other._Mypsi)), _Mysample_bitmap(std::move(_Other._Mysample_bitmap)), _Mysa_samples(std::move(_Other._Mysa_samples)), _Myisa_samples(std::move(_Other._Myisa_samples)) {}
        _Sab& operator=(const _Sab&) = delete;
        _Sab& operator=(_Sab&& _Other) noexcept { _Mysa_sample_rate = _Other._Mysa_sample_rate; _Myalphabet = std::move(_Other._Myalphabet); _Mypsi = std::move(_Other._Mypsi); _Mysample_bitmap = std::move(_Other._Mysample_bitmap); _Mysa_samples = std::move(_Other._Mysa_samples); _Myisa_samples = std::move(_Other._Myisa_samples); return *this; }
        template <class _SaCallbackFnT = _Dummy_sa_callback>
        void build(std::span<const _U8> _Text, _SaCallbackFnT&& _Sa_cb = _SaCallbackFnT{}) { _Prebuild_check(_Text); _Myalphabet = alphabet_type{_Text}; auto _Mapped_buf = _Map_text(_Text); _Do_build(std::span{_Mapped_buf.get(), _Text.size()}, std::forward<_SaCallbackFnT>(_Sa_cb)); }
        [[nodiscard]] size_type size() const noexcept { return _Mypsi.size(); }
        [[nodiscard]] bool empty() const { return size() == 0; }
        [[nodiscard]] size_type sa_sample_rate() const noexcept { return _Mysa_sample_rate; }
        [[nodiscard]] const alphabet_type& alphabet() const { return _Myalphabet; }
        [[nodiscard]] const psi_array_type& psi_array() const { return _Mypsi; }
        [[nodiscard]] size_type space_usage_in_bytes() const { return sizeof(_Mysa_sample_rate) + _Fastcsa_internal::_Space_usage_in_bytes(_Myalphabet) + _Fastcsa_internal::_Space_usage_in_bytes(_Mypsi) + _Fastcsa_internal::_Space_usage_in_bytes(_Mysample_bitmap) + _Fastcsa_internal::_Space_usage_in_bytes(_Mysa_samples) + _Fastcsa_internal::_Space_usage_in_bytes(_Myisa_samples); }
        [[nodiscard]] size_type count(std::span<const _U8> _Pattern) const {
            if (_Pattern.size() > size()) return 0;
            auto _Mapped_buf = _Map_text(_Pattern);
            return _Backward_search(std::span{_Mapped_buf.get(), _Pattern.size()}).length();
        }
        [[nodiscard]] size_type sa(size_type _Index) const {
            size_type _Step = 0, _Sample_index = 0;
            bool _Is_sampled = false;
            while (!_Is_sampled) {
                _Sample_index = _Mysample_bitmap.rank_one(_Index, _Is_sampled);
                ++_Step; _Index = _Mypsi[_Index];
            }
            const size_type _Sample_rate = _Get_sample_rate();
            size_type _Sample = _Mysa_samples[_Sample_index] * _Sample_rate;
            if (_Sample < _Step) return _Sample + size() - _Step + 1;
            return _Sample - _Step + 1;
        }
        [[nodiscard]] std::vector<size_type> locate(std::span<const _U8> _Pattern, bool _Sorted = false) const {
            auto _Mapped_buf = _Map_text(_Pattern);
            auto _Interval = _Backward_search(std::span{_Mapped_buf.get(), _Pattern.size()});
            if (_Interval.empty()) return {};
            std::vector<size_type> _Buf(_Interval.length());
            _Do_locate(_Interval, _Buf);
            if (_Sorted) std::sort(_Buf.begin(), _Buf.end());
            return _Buf;
        }
        [[nodiscard]] std::vector<_U8> extract(size_type _First, size_type _Count) const {
            if (_First > size() || _First + _Count > size()) throw std::out_of_range{"the extracted region is _Out of range"};
            std::vector<_U8> _Buf(_Count);
            _Do_extract(_First, _Count, _Buf);
            return _Buf;
        }
        void serialize(std::ostream& _Out) const { const size_type _Sample_rate = _Get_sample_rate(); _Out.write(reinterpret_cast<const char*>(&_Sample_rate), sizeof(_Sample_rate)); _Myalphabet.serialize(_Out); _Mypsi.serialize(_Out); _Mysample_bitmap.serialize(_Out); _Mysa_samples.serialize(_Out); _Myisa_samples.serialize(_Out); }
        void deserialize(std::istream& _In) { _In.read(reinterpret_cast<char*>(&_Mysa_sample_rate), sizeof(_Mysa_sample_rate)); _Validate_sample_rate(_Mysa_sample_rate); _Myalphabet.deserialize(_In); _Mypsi.deserialize(_In); _Mysample_bitmap.deserialize(_In); _Mysa_samples.deserialize(_In); _Myisa_samples.deserialize(_In); }
    private:
        size_type _Mysa_sample_rate{0};
        alphabet_type _Myalphabet;
        psi_array_type _Mypsi;
        _B_s1 _Mysample_bitmap;
        _P _Mysa_samples;
        _P _Myisa_samples;
        static void _Validate_sample_rate(size_type _Sa_sample_rate) {
            if (_Sa_sample_rate == 0) throw std::invalid_argument{"SA sample rate must be positive"};
        }
        [[nodiscard]] size_type _Get_sample_rate() const {
            _Validate_sample_rate(_Mysa_sample_rate);
            return _Mysa_sample_rate;
        }
        void _Prebuild_check(std::span<const _U8> _Text) {
            _Validate_sample_rate(_Mysa_sample_rate);
            if (!empty()) throw std::runtime_error{"current instance is not empty"};
            if (_Text.empty()) return;
            if (_Text.back() != '\0') throw std::invalid_argument{"last symbol must be '\\0'"};
        }
        template <class _SACallbackT>
        void _Do_build(std::span<const _U8> _Text, _SACallbackT&& _Sa_cb) {
            const size_t _Text_size = _Text.size();
            const size_type _Sample_rate = _Get_sample_rate();
            auto _Sa = std::make_unique_for_overwrite<_U64[]>(_Text_size);
            _Construct_suffix_array(_Text, std::span{_Sa.get(), _Text_size});
            std::unique_ptr<_U64[]> _Psi;
            size_type _Num_samples = _Integral_div_ceil(_Text_size, _Sample_rate);
            _U32 _Sample_width = _Safe_bit_width(_Num_samples - 1);
            std::vector<size_type> _Sampled_indices(_Num_samples);
            _Mysa_samples = _P(_Sample_width, _Num_samples);
            _Myisa_samples = _P(_Sample_width, _Num_samples);
            size_type _Sample_writer = 0;
            for (size_type _I = 0; _I < _Text_size; ++_I) {
                _U64 _Sa_value = _Sa[_I];
                if (_Sa_value % _Sample_rate == 0) {
                    _U64 _Sample = _Sa_value / _Sample_rate;
                    _Sampled_indices[_Sample_writer] = _I;
                    _Mysa_samples.set_value(_Sample_writer, _Sample);
                    _Myisa_samples.set_value(_Sample, _Sample_writer);
                    ++_Sample_writer;
                }
            }
            _Mysample_bitmap = _B_s1{_Text_size, _Sampled_indices.begin(), _Sampled_indices.begin() + _Sample_writer};
            std::invoke(std::forward<_SACallbackT>(_Sa_cb), std::span<size_type>{_Sa.get(), _Text_size});
            _Psi = std::make_unique_for_overwrite<_U64[]>(_Text_size);
            _Construct_psi(_Myalphabet, _Text, std::span{_Sa.get(), _Text_size}, std::span{_Psi.get(), _Text_size});
            _Sa.reset();
            _Mypsi.build(_Myalphabet, _Text, std::span{_Psi.get(), _Text_size});
        }
        [[nodiscard]] std::unique_ptr<_U8[]> _Map_text(std::span<const _U8> _Text) const {
            auto _Mapped = std::make_unique_for_overwrite<_U8[]>(_Text.size());
            std::ranges::transform(_Text, _Mapped.get(), [&](_U8 _C) { return _Myalphabet.map(_C); });
            return _Mapped;
        }
        [[nodiscard]] sa_interval_type _Backward_search(std::span<const _U8> _Pattern, size_type _L, size_type _R) const {
            if (_Pattern.empty()) return sa_interval_type{_L, _R};
            for (size_type _I = _Pattern.size(); _I > 0; --_I) {
                _U8 _C = _Pattern[_I - 1];
                size_type _Cur_l = _Myalphabet.counter(_C), _Cur_r = _Myalphabet.counter(_C + 1);
                _L = _Mypsi.lower_bound(sa_interval_type{_Cur_l, _Cur_r}, _L);
                _R = _Mypsi.lower_bound(sa_interval_type{_Cur_l, _Cur_r}, _R);
                if (_L >= _R) return sa_interval_type::make_empty();
            }
            return sa_interval_type{_L, _R};
        }
        [[nodiscard]] sa_interval_type _Backward_search(std::span<const _U8> _Pattern) const {
            _U8 _C = _Pattern.back();
            size_type _L = _Myalphabet.counter(_C), _R = _Myalphabet.counter(_C + 1);
            return _Backward_search(_Pattern.subspan(0, _Pattern.size() - 1), _L, _R);
        }
        void _Do_locate_memorized(sa_interval_type _Interval, std::span<size_type> _Buf) const {
            constexpr size_t _Unknown = static_cast<size_type>(-1);
            const size_type _Sample_rate = _Get_sample_rate();
            _I64 _Occ = _Interval.length();
            std::uninitialized_fill_n(_Buf.begin(), _Occ, _Unknown);
            std::vector<size_type> _Deps(_Occ, _Unknown);
            std::vector<size_type> _Steps_from_deps(_Occ, 0);
            for (size_type _I = _Interval._Min; _I < _Interval._Max; ++_I) {
                bool _Found_dep = false;
                size_type _Index = _I, _Step = 0;
                while (!_Mysample_bitmap.at(_Index)) {
                    _Index = _Mypsi[_Index]; _Step++;
                    if (_Interval.contains(_Index)) {
                        size_type _Dst_idx = _Index - _Interval._Min, _Src_idx = _I - _Interval._Min;
                        _Deps[_Dst_idx] = _Src_idx; _Steps_from_deps[_Src_idx] = _Step;
                        _Found_dep = true; break;
                    }
                }
                if (!_Found_dep) {
                    size_type _Sample = _Mysa_samples.get_value(_Mysample_bitmap.rank_one(_Index)) * _Sample_rate;
                    size_type _Pos = _Sample - _Step;
                    if (_Sample < _Step) _Pos = _Sample + size() - _Step;
                    _Buf[_I - _Interval._Min] = _Pos;
                }
            }
            for (size_type _I = 0; _I < _Occ; ++_I) {
                if (_Buf[_I] != _Unknown) {
                    size_type _Idx = _I;
                    while (_Deps[_Idx] != _Unknown) {
                        size_type _Dep_idx = _Deps[_Idx];
                        size_type _Pos = _Buf[_Idx], _Step = _Steps_from_deps[_Dep_idx];
                        size_type _Result = _Pos - _Step;
                        if (_Pos < _Step) _Result = _Pos + size() - _Step;
                        _Buf[_Dep_idx] = _Result;
                        _Deps[_Idx] = _Unknown; _Idx = _Dep_idx;
                    }
                }
            }
        }
        void _Do_locate_simple(sa_interval_type _Interval, std::span<size_type> _Buf) const { for (size_type _I = 0; _I < _Interval.length(); ++_I) _Buf[_I] = sa(_Interval._Min + _I); }
        void _Do_locate(sa_interval_type _Interval, std::span<size_type> _Buf) const { if (_Interval.length() >= 256) _Do_locate_memorized(_Interval, _Buf); else _Do_locate_simple(_Interval, _Buf); }
        void _Do_extract_ranked(size_type _Rank, size_type _Count, std::span<_U8> _Buf) const { for (size_type _I = 0; _I < _Count; ++_I) { _Buf[_I] = _Myalphabet.search_symbol(_Rank); _Rank = _Mypsi[_Rank]; } }
        [[nodiscard]] size_type _Position_to_rank(size_type _Pos) const {
            const size_type _Sample_rate = _Get_sample_rate();
            size_type _Sample_index = _Pos / _Sample_rate, _Step = _Pos % _Sample_rate;
            size_type _Sample = _Myisa_samples[_Sample_index];
            size_type _Index = _Mysample_bitmap.select(_Sample);
            for (size_type _I = 0; _I < _Step; ++_I) _Index = _Mypsi[_Index];
            return _Index;
        }
        void _Do_extract(size_type _First, size_type _Count, std::span<_U8> _Buf) const { size_type _Rank = _Position_to_rank(_First); _Do_extract_ranked(_Rank, _Count, _Buf); }
    };

    template <class _Ty>
    using _H = yaef::hybrid_list<_Ty, yaef::details::aligned_allocator<_U8, 32>,
        _Fastcsa_m_internal::_Is,
        _Fastcsa_m_internal::_Bv, 
        _Fastcsa_m_internal::_Re,
        _Fastcsa_m_internal::_Ef,
        _Fastcsa_m_internal::_Bg
    >;

    class _Psi : public _Psib<_Psi> {
        using _Base = _Psib<_Psi>;
        friend _Base;
        enum class _Me : _U32 { _Mix, _Pef, _Num_item };
        static constexpr _U32 _Num_me = static_cast<size_t>(_Me::_Num_item);
        static constexpr _U32 _Bw_me = std::bit_width(_Num_me - 1);
        [[nodiscard]] static std::string _To_string(_Me _Type);
    public:
        using typename _Base::size_type;
        using typename _Base::value_type;
        _Psi() = default;
        _Psi(_Psi&& _Other) noexcept = default;
        _Psi& operator=(_Psi&& _Other) noexcept = default;
    private:
        class _O {};

        class _Mix : public _O { using _Base_list_type = _H<_U64>; public:
            _Mix() = default;
            _Mix(const _Mix&) = default;
            _Mix(_Mix&&) = default;
            _Mix([[maybe_unused]] const _Psi&, std::span<const value_type> _Vals) : _Mylist(yaef::from_sorted, _Vals.begin(), _Vals.end()) {}
            _Mix& operator=(const _Mix&) = default;
            _Mix& operator=(_Mix&&) = default;
            [[nodiscard]] static size_type estimate_space_usage_in_bytes([[maybe_unused]] const _Psi&, std::span<const value_type> _Vals) { return _Base_list_type::estimate_required_bits(_Vals.begin(), _Vals.end()) / 8; }
            [[nodiscard]] size_type space_usage_in_bytes() const { return _Mylist.space_usage_in_bytes(); }
            void serialize(std::ostream& _Out) const { yaef::serialize_to_stream(_Mylist, _Out); }
            void deserialize(std::istream& _In) { yaef::deserialize_from_stream(_Mylist, _In); }
            [[nodiscard]] const _Base_list_type& base_list() const { return _Mylist; }
            [[nodiscard]] value_type at([[maybe_unused]] const _Psi&, size_type _Offset) const { return _Mylist.at(_Offset); }
            [[nodiscard]] size_type lower_bound([[maybe_unused]] const _Psi&, [[maybe_unused]] _Interval<size_type>, value_type _Target) const { return _Mylist.index_of_lower_bound(_Target); }
        private: _Base_list_type _Mylist;
        };
        class _Pef : public _O {
        public:
            _Pef() = default;
            _Pef(const _Pef&) = default;
            _Pef(_Pef&&) = default;
            _Pef(const _Psi&, std::span<const value_type> _Vals) {
                auto [_Starts, _Offsets] = _Encode_run_length(_Vals);
                _Mystarts = yaef::eliasfano_list<value_type>(yaef::from_sorted, _Starts.begin(), _Starts.end());
                _Myends = yaef::eliasfano_list<size_type>(yaef::from_sorted, _Offsets.begin(), _Offsets.end());
            }
            _Pef& operator=(const _Pef&) = default;
            _Pef& operator=(_Pef&&) = default;
            [[nodiscard]] static size_type estimate_space_usage_in_bytes([[maybe_unused]] const _Psi&, std::span<const value_type> _Vals) {
                auto [_Starts, _Offsets] = _Encode_run_length(_Vals);
                auto _Calc_list_usage = [](const auto& _List) -> size_type {
                    value_type _Min = _List.front(); value_type _Max = _List.back(); value_type _U = _Max - _Min; size_type _N = _List.size();
                    _U32 _Low_width = _Safe_bit_width(static_cast<_U64>(_U) / _N);
                    size_type _Num_low_bits = _Low_width * _N;
                    size_type _Num_low_bytes = _Integral_div_ceil(_Num_low_bits, 64) * sizeof(yaef::packed_int_buffer<>::block_type);
                    size_type _Num_high_bits = _N + (_U >> _Low_width) + 1;
                    _Num_high_bits = static_cast<size_type>(static_cast<double>(_Num_high_bits) * 1.5);
                    size_type _Num_high_bytes = _Integral_div_ceil(_Num_high_bits, 64) * sizeof(yaef::packed_int_buffer<>::block_type);
                    return _Num_low_bytes + _Num_high_bytes;
                };
                return _Calc_list_usage(_Starts) + _Calc_list_usage(_Offsets);
            }
            [[nodiscard]] size_type space_usage_in_bytes() const { return _Mystarts.space_usage_in_bytes() + _Myends.space_usage_in_bytes(); }
            void serialize(std::ostream& _Out) const { yaef::serialize_to_stream(_Mystarts, _Out); yaef::serialize_to_stream(_Myends, _Out); }
            void deserialize(std::istream& _In) { yaef::deserialize_from_stream(_Mystarts, _In); yaef::deserialize_from_stream(_Myends, _In); }
            [[nodiscard]] value_type at(const _Psi&, size_type _Offset) const {
                auto _Iter = _Myends.lower_bound(_Offset);
                size_type _Idx = _Iter.to_index();
                value_type _Start_val = _Mystarts[_Idx];
                size_type _Base = _Idx == 0 ? 0 : *std::prev(_Iter) + 1;
                return _Start_val + _Offset - _Base;
            }
            [[nodiscard]] size_type lower_bound(const _Psi&, _Interval<size_type>, value_type _Target) const {
                if (_Target <= _Mystarts.front()) return 0;
                auto _Iter = std::prev(_Mystarts.upper_bound(_Target));
                size_type _Idx = _Iter.to_index();
                _Target -= *_Iter;
                auto _End_iter = _Myends.iter(_Idx);
                size_type _Base = _Idx == 0 ? 0 : (*std::prev(_End_iter) + 1);
                size_type _Cand_idx = _Base + _Target;
                return std::min<size_type>(_Cand_idx, *_End_iter + 1);
            }
        private:
            yaef::eliasfano_list<value_type> _Mystarts;
            yaef::eliasfano_list<size_type> _Myends;
        };

        using _M = _Wp<_Bw_me>;

        std::list<_Mix> _Mymix_pool;
        std::list<_Pef> _Mypef_pool;
        _M _Mymethods;
        std::vector<_O*> _Mylists;

        [[nodiscard]] _Me _Get_me(size_type _Idx) const { return static_cast<_Me>(_Mymethods.get_value(_Idx)); }

        [[nodiscard]] static std::pair<std::vector<value_type>, std::vector<size_type>> _Encode_run_length(std::span<const value_type> _Vals) {
            std::vector<value_type> _Starts; std::vector<size_type> _Ends;
            _Starts.push_back(_Vals[0]);
            for (size_t _I = 1; _I < _Vals.size(); ++_I) {
                if (_Vals[_I] != _Vals[_I - 1] + 1) { _Ends.push_back(_I - 1); _Starts.push_back(_Vals[_I]); }
            }
            _Ends.push_back(_Vals.size() - 1);
            return {std::move(_Starts), std::move(_Ends)};
        }

        template <class _Fn>
        auto _Dispatch(_Me _Type, const _O* _Sub, _Fn&& _Func) const {
            switch (_Type) {
            default: _Mark_as_unreachable();
            case _Me::_Mix: return std::invoke(std::forward<_Fn>(_Func), static_cast<const _Mix&>(*_Sub));
            case _Me::_Pef: return std::invoke(std::forward<_Fn>(_Func), static_cast<const _Pef&>(*_Sub));
            }
        }

        template <class _Fn>
        auto _Dispatch(_Me _Type, _O* _Sub, _Fn&& _Func) {
            switch (_Type) {
            default: _Mark_as_unreachable();
            case _Me::_Mix: return std::invoke(std::forward<_Fn>(_Func), static_cast<_Mix&>(*_Sub));
            case _Me::_Pef: return std::invoke(std::forward<_Fn>(_Func), static_cast<_Pef&>(*_Sub));
            }
        }

        [[nodiscard]] size_type _Do_space_usage_in_bytes() const noexcept;
        [[nodiscard]] value_type _Do_get_psi(size_type _Idx, size_type _Sub_idx, _Interval<size_type> _Interval) const noexcept;
        [[nodiscard]] size_type _Do_lower_bound(size_type _Sub_idx, _Interval<size_type> _Interval, value_type _Target) const;

        [[nodiscard]] _O* _Alloc_block(_Me _Type);
        void _Encode_block(_O* _Block, _Me _Type, std::span<const value_type> _Span);
        void _Do_build(std::span<const value_type> _Psi);
        void _Do_swap(_Psi& _Other) noexcept;
        void _Do_serialize(std::ostream& _Out) const;
        void _Do_deserialize(std::istream& _In);
    };

}

namespace fastcsa {

    using csa = _Fastcsa_internal::_Sab<_Fastcsa_internal::_Psi>;

}
