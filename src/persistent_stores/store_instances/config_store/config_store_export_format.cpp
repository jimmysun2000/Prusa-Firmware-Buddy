#include "config_store_export_format.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace config_store_export {

namespace {

    /// Line buffer that silently cuts anything beyond max_line_length
    class Line {
    public:
        void append(std::string_view text) {
            const size_t count = std::min(text.size(), max_line_length - length_);
            std::copy_n(text.data(), count, buffer_.data() + length_);
            length_ += count;
        }

        void append_char(char c) {
            append(std::string_view(&c, 1));
        }

        [[gnu::format(printf, 2, 3)]] void append_printf(const char *format, ...) {
            va_list args;
            va_start(args, format);
            const int written = vsnprintf(buffer_.data() + length_, buffer_.size() - length_, format, args);
            va_end(args);
            if (written > 0) {
                length_ = std::min(length_ + static_cast<size_t>(written), max_line_length);
            }
        }

        size_t length() const {
            return length_;
        }

        std::string_view view() const {
            return { buffer_.data(), length_ };
        }

    private:
        std::array<char, max_line_length + 1> buffer_;
        size_t length_ = 0;
    };

    /// inih splits on '=', starts comments with '#' and sections with '['
    constexpr bool is_name_char(char c) {
        return c >= ' ' && c <= '~' && c != '=' && c != '#' && c != ';' && c != '[' && c != ']';
    }

    constexpr bool is_plain_string_char(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    }

    void start_line(Line &line, const Key &key, size_t byte_offset = 0) {
        line.append_printf("%04" PRIX16, key.id);
        if (byte_offset > 0) {
            line.append_printf("+%u", static_cast<unsigned>(byte_offset));
        }
        line.append_char(' ');

        for (const char c : key.name.substr(0, max_name_length)) {
            line.append_char(is_name_char(c) ? c : '_');
        }
        if (key.index.has_value()) {
            line.append_printf("[%u]", static_cast<unsigned>(*key.index));
        }

        line.append(" = ");
    }

    char hex_digit(uint8_t value) {
        return "0123456789ABCDEF"[value & 0xf];
    }

} // namespace

void write_bool(LineSink &sink, const Key &key, bool value) {
    Line line;
    start_line(line, key);
    line.append(value ? "bool:1" : "bool:0");
    sink.write_line(line.view());
}

void write_unsigned(LineSink &sink, const Key &key, uint32_t value, size_t size) {
    Line line;
    start_line(line, key);
    line.append_printf("u%u:%" PRIu32, static_cast<unsigned>(size * 8), value);
    sink.write_line(line.view());
}

void write_signed(LineSink &sink, const Key &key, int32_t value, size_t size) {
    Line line;
    start_line(line, key);
    line.append_printf("i%u:%" PRIi32, static_cast<unsigned>(size * 8), value);
    sink.write_line(line.view());
}

void write_float(LineSink &sink, const Key &key, float value) {
    Line line;
    start_line(line, key);
    line.append_printf("f32:%.9g", static_cast<double>(value));
    sink.write_line(line.view());
}

void write_string(LineSink &sink, const Key &key, std::span<const char> text) {
    const auto end = std::find(text.begin(), text.end(), '\0');
    const auto length = static_cast<size_t>(end - text.begin());

    if (length > hex_bytes_per_line * 2 || !std::all_of(text.begin(), end, is_plain_string_char)) {
        write_raw(sink, key, std::as_bytes(text.first(length)));
        return;
    }

    Line line;
    start_line(line, key);
    line.append("str:");
    line.append(std::string_view(text.data(), length));
    sink.write_line(line.view());
}

void write_raw(LineSink &sink, const Key &key, std::span<const std::byte> bytes, std::string_view comment) {
    size_t offset = 0;
    do {
        const auto chunk = bytes.subspan(offset, std::min(hex_bytes_per_line, bytes.size() - offset));

        Line line;
        start_line(line, key, offset);
        line.append("hex:");
        for (const std::byte b : chunk) {
            line.append_char(hex_digit(static_cast<uint8_t>(b) >> 4));
            line.append_char(hex_digit(static_cast<uint8_t>(b)));
        }
        if (offset == 0 && !comment.empty()) {
            line.append(" # ");
            for (const char c : comment) {
                line.append_char(c >= ' ' && c <= '~' ? c : '_');
            }
        }
        sink.write_line(line.view());

        offset += chunk.size();
    } while (offset < bytes.size());
}

void write_redacted(LineSink &sink, const Key &key) {
    Line line;
    start_line(line, key);
    line.append("redacted:");
    sink.write_line(line.view());
}

bool is_redacted(uint16_t id, bool is_string, std::span<const uint16_t> secret_ids, std::span<const uint16_t> allowed_string_ids) {
    if (std::ranges::find(secret_ids, id) != secret_ids.end()) {
        return true;
    }
    return is_string && std::ranges::find(allowed_string_ids, id) == allowed_string_ids.end();
}

} // namespace config_store_export
