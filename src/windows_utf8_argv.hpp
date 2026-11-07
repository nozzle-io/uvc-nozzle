#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

// Own UTF-8 bytes until the existing parser and application have returned.
class windows_utf8_argv {
public:
    windows_utf8_argv() = default;
    windows_utf8_argv(const windows_utf8_argv &) = delete;
    windows_utf8_argv &operator=(const windows_utf8_argv &) = delete;

    bool assign(int argc, wchar_t **wide_argv) {
        if(argc < 1 || wide_argv == nullptr) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
        storage_.resize(static_cast<std::size_t>(argc));
        for(int index = 0; index < argc; ++index) {
            if(wide_argv[index] == nullptr) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                wide_argv[index], -1, nullptr, 0, nullptr, nullptr);
            if(size == 0) return false;
            auto &text = storage_[static_cast<std::size_t>(index)];
            text.resize(static_cast<std::size_t>(size));
            if(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                wide_argv[index], -1, text.data(), size, nullptr, nullptr) != size) return false;
        }
        // Build pointers only after the strings stop moving; retain argv[argc].
        pointers_.reserve(static_cast<std::size_t>(argc) + 1u);
        pointers_.clear();
        for(auto &text : storage_) pointers_.push_back(text.data());
        pointers_.push_back(nullptr);
        return true;
    }

    char **data() noexcept { return pointers_.data(); }

private:
    std::vector<std::string> storage_;
    std::vector<char *> pointers_;
};
#endif
