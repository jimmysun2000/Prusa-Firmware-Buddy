#pragma once

#include <algorithm>
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

/// Longest name the owner can give a macro
inline constexpr size_t max_name_length = 15;

/// A name, zero-terminated and zero-padded; empty for the default label
using Name = std::array<char, max_name_length + 1>;

/// Size of the parameter buffer a label needs: the longest name and its terminator
inline constexpr size_t label_buffer_size = max_name_length + 1;

/// @returns @p name ending in its terminator, every character before it outside ' ' to '~' replaced by '?'
constexpr Name sanitized(Name name) {
    name.back() = '\0';
    for (char &c : name) {
        if (c == '\0') {
            break;
        }
        if (c < ' ' || c > '~') {
            c = '?';
        }
    }
    return name;
}

/// @returns @p name without leading and trailing spaces, zero-padded; empty if it has only spaces
constexpr Name trimmed(const Name &name) {
    std::string_view text { name.data(), static_cast<size_t>(std::find(name.begin(), name.end(), '\0') - name.begin()) };
    const size_t first = text.find_first_not_of(' ');
    text = (first == std::string_view::npos) ? std::string_view {} : text.substr(first, text.find_last_not_of(' ') - first + 1);

    Name result {};
    std::copy(text.begin(), text.end(), result.begin());
    return result;
}

/// @returns the owner-set name of macro @p n; empty if it has none
Name name(uint8_t n);

/// Stores @p name for macro @p n, trimmed; an empty name brings back the default label
void set_name(uint8_t n, const Name &name);

/// Home screen icon of a macro. The values are stored: never change or reuse one.
enum class Icon : uint8_t {
    play = 0,
    home = 1,
    stop = 2,
    pause = 3,
    check = 4,
    cross = 5,
    repeat = 6,
    back = 7,
    branch = 8,
    spools = 9,
};

/// In the order of the selection menu
inline constexpr std::array icons {
    Icon::play,
    Icon::home,
    Icon::stop,
    Icon::pause,
    Icon::check,
    Icon::cross,
    Icon::repeat,
    Icon::back,
    Icon::branch,
    Icon::spools,
};
static_assert(icons[0] == Icon::play);

/// @returns the index in icons of the stored icon @p id; that of Play, the default, for an unknown id
constexpr size_t icon_index_of(uint8_t id) {
    for (size_t i = 0; i < icons.size(); ++i) {
        if (static_cast<uint8_t>(icons[i]) == id) {
            return i;
        }
    }
    return 0;
}

Icon icon(uint8_t n);

void set_icon(uint8_t n, Icon icon);

/// @returns the name of @p icon in the selection menu, untranslated
const char *icon_label(Icon icon);

/// @returns the name of macro @p n cut to @p max_length characters, or "Macro <n>" if it has none, formatted into @p params.
/// The name is shown as it is, whatever characters it has.
/// @param params needs label_buffer_size bytes
string_view_utf8 label(uint8_t n, StringViewUtf8ParamBase &params, size_t max_length = max_name_length);

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
