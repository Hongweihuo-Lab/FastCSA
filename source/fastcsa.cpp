#include "fastcsa/fastcsa.hpp"

#include <stdexcept>

#include "libsais64.h"

namespace _Fastcsa_internal {

    _Ch::_Ch(std::span<const symbol_type> _Text)
        : _Mysize(0) {
        _Myflags.resize(max_size);
        _Mycounters.resize(max_size + 1, 0);
        _Mymap.resize(max_size);
        _Myunmap.resize(max_size);

        for (_U64 _I = 0; _I < _Text.size(); ++_I) {
            symbol_type _Value = _Text[_I];
            ++_Mycounters[static_cast<symbol_type>(_Value)];
            _Myflags.set_bit(_Value, true);
        }

        size_type _Acc = 0;
        for (size_type _I = 0; _I <= max_size; ++_I) {
            if (_Mycounters[_I] != 0) {
                _Mymap[_I] = static_cast<symbol_type>(_Mysize);
                _Myunmap[_Mysize++] = static_cast<symbol_type>(_I);
            }
            size_type _Val = _Acc;
            _Acc += _Mycounters[_I];
            _Mycounters[_I] = _Val;
        }
        [[maybe_unused]] auto _New_end = std::unique(_Mycounters.begin(), _Mycounters.end());
        _Mycounters.resize(size() + 1);
        _Myunmap.resize(size());
    }

    void _Ch::serialize(std::ostream& _Out) const {
        _Out.write(reinterpret_cast<const char*>(&_Mysize), sizeof(_Mysize));
        _Myflags.serialize(_Out);

        auto _Write_vec = [&](const auto& _Vec) {
            using _Type = typename std::remove_cvref_t<decltype(_Vec)>::value_type;
            size_t _Num = _Vec.size();
            _Out.write(reinterpret_cast<const char*>(&_Num), sizeof(_Num));
            _Out.write(reinterpret_cast<const char*>(_Vec.data()), sizeof(_Type) * _Num);
        };

        _Write_vec(_Mycounters);
        _Write_vec(_Mymap);
        _Write_vec(_Myunmap);
    }

    void _Ch::deserialize(std::istream& _In) {
        _In.read(reinterpret_cast<char*>(&_Mysize), sizeof(_Mysize));
        _Myflags.deserialize(_In);

        auto _Read_vec = [&](auto& _Vec) {
            using _Type = typename std::remove_cvref_t<decltype(_Vec)>::value_type;
            size_t _Num;
            _In.read(reinterpret_cast<char*>(&_Num), sizeof(_Num));
            _Vec.resize(_Num);
            _In.read(reinterpret_cast<char*>(_Vec.data()), sizeof(_Type) * _Num);
        };

        _Read_vec(_Mycounters);
        _Read_vec(_Mymap);
        _Read_vec(_Myunmap);
    }

    std::string _Psi::_To_string(_Me _Type) {
        switch (_Type) {
        case _Me::_Mix: return "mix";
        case _Me::_Pef: return "pef";
        default: return "";
        }
    }

    auto _Psi::_Alloc_block(_Me _Type) -> _O* {
        _O* _Block = nullptr;
        switch (_Type) {
        default: _Mark_as_unreachable();
        case _Me::_Mix:
            _Block = std::addressof(_Mymix_pool.emplace_back());
            break;
        case _Me::_Pef:
            _Block = std::addressof(_Mypef_pool.emplace_back());
            break;
        }
        return _Block;
    }

    void _Psi::_Encode_block(_O* _Block, _Me _Type, std::span<const value_type> _Psi_span) {
        switch (_Type) {
        default: _Mark_as_unreachable();
        case _Me::_Mix:
            static_cast<_Mix&>(*_Block) = _Mix{*this, _Psi_span};
            break;
        case _Me::_Pef:
            static_cast<_Pef&>(*_Block) = _Pef{*this, _Psi_span};
            break;
        }
    }

    void _Psi::_Do_build(std::span<const value_type> _Psi) {
        size_type _Num_subcontexts = this->_Mysubctx_indexer.num_contexts();
        _Mylists.resize(_Num_subcontexts, nullptr);
        std::vector<_Me> _Unpacked_block_types(_Num_subcontexts);

        for (size_type _I = 0; _I < _Num_subcontexts; ++_I) {
            auto _Subctx_interval = this->_Mysubctx_indexer.interval_of(_I);
            auto _Psi_span = std::span{_Psi.data() + _Subctx_interval._Min, _Psi.data() + _Subctx_interval._Max};

            constexpr size_type _Num_candidates = _To_underlying(_Me::_Num_item);
            size_type _Candidate_space_usages[_Num_candidates]{};
            _Candidate_space_usages[_To_underlying(_Me::_Mix)] =
                _Mix::estimate_space_usage_in_bytes(*this, _Psi_span);
            _Candidate_space_usages[_To_underlying(_Me::_Pef)] =
                _Pef::estimate_space_usage_in_bytes(*this, _Psi_span);

            auto _Best_block_type = static_cast<_Me>(
                std::distance(_Candidate_space_usages, std::min_element(
                    _Candidate_space_usages, _Candidate_space_usages + _Num_candidates)));

            _O* _New_block = _Alloc_block(_Best_block_type);
            _Encode_block(_New_block, _Best_block_type, _Psi_span);
            _Mylists[_I] = _New_block;
            _Unpacked_block_types[_I] = _Best_block_type;
        }

        _Mymethods = _M{_Num_subcontexts};
        for (size_type _I = 0; _I < _Unpacked_block_types.size(); ++_I) {
            _Mymethods.set_value(_I, static_cast<_U64>(_Unpacked_block_types[_I]));
        }
    }

    void _Psi::_Do_swap(_Psi& _Other) noexcept {
        _Mymix_pool.swap(_Other._Mymix_pool);
        _Mypef_pool.swap(_Other._Mypef_pool);
        _Mymethods.swap(_Other._Mymethods);
        _Mylists.swap(_Other._Mylists);
    }

    void _Psi::_Do_serialize(std::ostream& _Out) const {
        _Mymethods.serialize(_Out);
        size_type _Num_blocks = _Mylists.size();
        _Out.write(reinterpret_cast<const char*>(&_Num_blocks), sizeof(_Num_blocks));
        for (size_type _I = 0; _I < _Num_blocks; ++_I) {
            auto _Block_type = static_cast<_Me>(_Mymethods.get_value(_I));
            _Dispatch(_Block_type, _Mylists[_I], [&](const auto& _Blk) {
                _Blk.serialize(_Out);
            });
        }
    }

    void _Psi::_Do_deserialize(std::istream& _In) {
        _Mymethods.deserialize(_In);
        size_type _Num_blocks;
        _In.read(reinterpret_cast<char*>(&_Num_blocks), sizeof(_Num_blocks));

        _Mylists.resize(_Num_blocks);
        for (size_type _I = 0; _I < _Num_blocks; ++_I) {
            auto _Block_type = static_cast<_Me>(_Mymethods.get_value(_I));
            switch (_Block_type) {
            default: _Mark_as_unreachable();
            case _Me::_Mix: {
                auto& _Blk = _Mymix_pool.emplace_back();
                _Blk.deserialize(_In);
                _Mylists[_I] = std::addressof(_Blk);
            } break;
            case _Me::_Pef: {
                auto& _Blk = _Mypef_pool.emplace_back();
                _Blk.deserialize(_In);
                _Mylists[_I] = std::addressof(_Blk);
            } break;
            }
        }
    }

    size_t _Psi::_Do_space_usage_in_bytes() const noexcept {
        size_t _Result = 0;
        for (const auto& _Sublist : _Mymix_pool) {
            _Result += _Sublist.space_usage_in_bytes();
        }
        for (const auto& _Sublist : _Mypef_pool) {
            _Result += _Sublist.space_usage_in_bytes();
        }
        _Result += _Mymethods.space_usage_in_bytes();
        return _Result;
    }

    auto _Psi::_Do_get_psi(size_type _Idx, size_type _Sub_idx, _Interval<size_type> _Interval) const noexcept -> value_type {
        size_type _Block_offset = _Idx - _Interval._Min;
        const _O* _Block = _Mylists[_Sub_idx];
        _Me _Block_type = static_cast<_Me>(_Mymethods.get_value(_Sub_idx));

        return _Dispatch(_Block_type, _Block, [&](const auto& _Blk) {
            return _Blk.at(*this, _Block_offset);
        });
    }

    size_t _Psi::_Do_lower_bound(size_type _Sub_idx, _Interval<size_type> _Interval, value_type _Target) const {
        size_type _Offset = _Dispatch(_Get_me(_Sub_idx), _Mylists[_Sub_idx],
            [&](const auto& _Block) { return _Block.lower_bound(*this, _Interval, _Target); }
        );
        return _Interval._Min + _Offset;
    }

    void _Construct_psi(const _Ch& _Alphabet, std::span<const _U8> _Text, std::span<const _U64> _Sa, std::span<_U64> _Psi) {
        if (_Text.size() != _Sa.size()) {
            throw std::invalid_argument("the size of text must be the same as the size of suffix array");
        }
        if (_Text.size() > _Psi.size()) {
            throw std::out_of_range("the result buffer is too small");
        }

        auto _Bkts = std::make_unique_for_overwrite<size_t[]>(_Alphabet.size());
        for (_U64 _I = 0; _I < _Alphabet.size(); ++_I) {
            _Bkts[_I] = _Alphabet.counter(_I);
        }

        for (_U64 _I = 0; _I < _Sa.size(); ++_I) {
            const _U64 _Suf = _Sa[_I];
            _U64 _Prv = _Suf == 0 ? _Text.size() - 1 : _Suf - 1;
            _Psi[_Bkts[_Text[_Prv]]++] = _I;
        }
    }

    void _Construct_suffix_array(std::span<const _U8> _Text, std::span<_U64> _Sa) {
        if (_Text.size() > _Sa.size()) {
            throw std::out_of_range{"the result buffer is too small"};
        }

        int _Ret = libsais64(reinterpret_cast<const uint8_t *>(_Text.data()), reinterpret_cast<int64_t *>(_Sa.data()), _Text.size(), 0, nullptr);
        if (_Ret != 0) {
            throw std::runtime_error{"failed to construct suffix array"};
        }
    }

}
