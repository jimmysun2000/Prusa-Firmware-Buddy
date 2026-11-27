#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

class string_view_utf8;
class StringViewUtf8ParamBase;

/// User G-code macros: the files /usb/macros/btn_1.gcode to btn_8.gcode, run through the inject queue
namespace user_macro {

inline constexpr uint8_t count = 8;

/// Longest file the loader is sure to buffer whole: the buffer also holds the terminating null
inline constexpr size_t max_file_size = 1023;

/// Longest line the loader reads without cutting off the rest
inline constexpr size_t max_line_length = 80;

enum class Fault : uint8_t {
    none,
    file_too_long,
    line_too_long,
};

/// Checks the raw content of a macro file against the loader limits, fed in parts
class FileCheck {
public:
    constexpr void feed(std::string_view part) {
        for (const char c : part) {
            if (fault_ != Fault::none) {
                return;
            }
            if (++size_ > max_file_size) {
                fault_ = Fault::file_too_long;
            } else if (c == '\r' || c == '\n') {
                line_length_ = 0;
            } else if (++line_length_ > max_line_length) {
                fault_ = Fault::line_too_long;
            }
        }
    }

    /// The first limit the content fed so far breaks
    constexpr Fault fault() const {
        return fault_;
    }

private:
    size_t size_ = 0;
    size_t line_length_ = 0;
    Fault fault_ = Fault::none;
};

/// @returns "Macro <n>", formatted into @p params
string_view_utf8 label(uint8_t n, StringViewUtf8ParamBase &params);

using Path = std::array<char, 32>;

/// @returns the path of the file of macro @p n
Path file_path(uint8_t n);

bool file_exists(uint8_t n);

/// Runs macro @p n (1 to count) if the printer is idle and the file passes the checks.
/// Otherwise shows a message that says why it does not run.
/// Once started, a macro cannot be stopped.
/// @param confirm ask Yes/No before it starts, with No focused
void run(uint8_t n, bool confirm = false);

} // namespace user_macro
