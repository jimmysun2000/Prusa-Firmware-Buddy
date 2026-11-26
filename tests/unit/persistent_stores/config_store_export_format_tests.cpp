#include <config_store/config_store_export_format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace config_store_export;

namespace {

class VectorSink final : public LineSink {
public:
    void write_line(std::string_view line) override {
        lines.emplace_back(line);
    }

    std::vector<std::string> lines;
};

std::vector<std::string> lines_of(auto &&write) {
    VectorSink sink;
    write(sink);
    return sink.lines;
}

} // namespace

TEST_CASE("config_store_export: scalar lines") {
    CHECK(lines_of([](LineSink &s) { write_unsigned(s, { 0x1F3A, "Sound Volume" }, 5, 1); }) == std::vector<std::string> { "1F3A Sound Volume = u8:5" });
    CHECK(lines_of([](LineSink &s) { write_unsigned(s, { 0x0042, "Port" }, 8514, 2); }) == std::vector<std::string> { "0042 Port = u16:8514" });
    CHECK(lines_of([](LineSink &s) { write_unsigned(s, { 0xABCD, "Time" }, 4000000000u, 4); }) == std::vector<std::string> { "ABCD Time = u32:4000000000" });
    CHECK(lines_of([](LineSink &s) { write_signed(s, { 0x0001, "Timezone" }, -5, 1); }) == std::vector<std::string> { "0001 Timezone = i8:-5" });
    CHECK(lines_of([](LineSink &s) { write_signed(s, { 0x0002, "Sens" }, -32768, 2); }) == std::vector<std::string> { "0002 Sens = i16:-32768" });
    CHECK(lines_of([](LineSink &s) { write_bool(s, { 0x3FFF, "Enabled" }, true); }) == std::vector<std::string> { "3FFF Enabled = bool:1" });
    CHECK(lines_of([](LineSink &s) { write_bool(s, { 0x3FFF, "Enabled" }, false); }) == std::vector<std::string> { "3FFF Enabled = bool:0" });
    CHECK(lines_of([](LineSink &s) { write_float(s, { 0x0100, "Diameter" }, 0.4f); }) == std::vector<std::string> { "0100 Diameter = f32:0.400000006" });
    CHECK(lines_of([](LineSink &s) { write_float(s, { 0x0100, "Zero" }, 0.0f); }) == std::vector<std::string> { "0100 Zero = f32:0" });
}

TEST_CASE("config_store_export: array index in the key") {
    const Key key { .id = 0x2B12, .name = "Loaded Filament", .index = 2 };
    CHECK(lines_of([&](LineSink &s) { write_unsigned(s, key, 7, 1); }) == std::vector<std::string> { "2B12 Loaded Filament[2] = u8:7" });
}

TEST_CASE("config_store_export: names are sanitized and cut") {
    CHECK(lines_of([](LineSink &s) { write_bool(s, { 0x0001, "a=b#c;d[e]f\tg" }, true); }) == std::vector<std::string> { "0001 a_b_c_d_e_f_g = bool:1" });

    const std::string long_name(100, 'n');
    const auto lines = lines_of([&](LineSink &s) { write_bool(s, { 0x0001, long_name }, true); });
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "0001 " + std::string(max_name_length, 'n') + " = bool:1");

    CHECK(lines_of([](LineSink &s) { write_bool(s, { 0x0001, "" }, true); }) == std::vector<std::string> { "0001  = bool:1" });
}

TEST_CASE("config_store_export: strings") {
    const std::array<char, 21> host { "prusa-xl_01.local" };
    CHECK(lines_of([&](LineSink &s) { write_string(s, { 0x1234, "Hostname" }, host); }) == std::vector<std::string> { "1234 Hostname = str:prusa-xl_01.local" });

    const std::array<char, 8> empty {};
    CHECK(lines_of([&](LineSink &s) { write_string(s, { 0x1234, "Hostname" }, empty); }) == std::vector<std::string> { "1234 Hostname = str:" });

    // Anything outside [A-Za-z0-9._-] goes out as hex of the text (up to the zero)
    const std::array<char, 33> ssid { "My Wifi" };
    CHECK(lines_of([&](LineSink &s) { write_string(s, { 0x0C40, "WIFI AP SSID" }, ssid); }) == std::vector<std::string> { "0C40 WIFI AP SSID = hex:4D792057696669" });

    const std::array<char, 4> unterminated { 'a', 'b', 'c', 'd' };
    CHECK(lines_of([&](LineSink &s) { write_string(s, { 0x0001, "S" }, unterminated); }) == std::vector<std::string> { "0001 S = str:abcd" });
}

TEST_CASE("config_store_export: raw values") {
    const std::array<uint8_t, 12> offset { 0x00, 0x00, 0x80, 0x3E, 0x0A, 0xD7, 0x23, 0x3C, 0x8F, 0xC2, 0xF5, 0xBC };
    CHECK(lines_of([&](LineSink &s) { write_raw(s, { 0x2B10, "Tool Offset 1" }, std::as_bytes(std::span(offset)), "x=0.25 y=0.01 z=-0.03"); })
        == std::vector<std::string> { "2B10 Tool Offset 1 = hex:0000803E0AD7233C8FC2F5BC # x=0.25 y=0.01 z=-0.03" });

    CHECK(lines_of([&](LineSink &s) { write_raw(s, { 0x2B10, "T" }, std::as_bytes(std::span(offset))); }) == std::vector<std::string> { "2B10 T = hex:0000803E0AD7233C8FC2F5BC" });

    // Long values continue on lines keyed with the byte offset, the comment goes to the first line only
    std::array<uint8_t, 100> big;
    for (size_t i = 0; i < big.size(); i++) {
        big[i] = static_cast<uint8_t>(i);
    }
    const auto lines = lines_of([&](LineSink &s) { write_raw(s, { 0x0ABC, "Big" }, std::as_bytes(std::span(big)), "note"); });
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].starts_with("0ABC Big = hex:000102"));
    CHECK(lines[0].ends_with("2F # note"));
    CHECK(lines[1].starts_with("0ABC+48 Big = hex:3031"));
    CHECK(lines[1].ends_with("5F"));
    CHECK(lines[2] == "0ABC+96 Big = hex:60616263");

    // Total hex digits match the data
    size_t digits = 0;
    for (const auto &line : lines) {
        const auto start = line.find("hex:") + 4;
        const auto end = line.find(" #");
        digits += (end == std::string::npos ? line.size() : end) - start;
    }
    CHECK(digits == big.size() * 2);
}

TEST_CASE("config_store_export: no line exceeds the limit") {
    const std::string long_name(300, 'x');
    const std::string long_comment(300, 'c');
    std::array<uint8_t, 512> data {};
    const std::array<char, 200> text { '#' };

    VectorSink sink;
    const Key key { .id = 0xFFFF, .name = long_name, .index = 255 };
    write_raw(sink, key, std::as_bytes(std::span(data)), long_comment);
    write_string(sink, key, text);
    write_unsigned(sink, key, 4294967295u, 4);
    write_signed(sink, key, -2147483647 - 1, 4);
    write_float(sink, key, -1.17549435e-38f);
    write_redacted(sink, key);
    write_bool(sink, key, true);

    REQUIRE(sink.lines.size() == 11 + 1 + 5);
    for (const auto &line : sink.lines) {
        CHECK(line.size() <= max_line_length);
    }

    // Values are never cut: only the comment may be
    for (size_t i = 0; i < 11; i++) {
        const auto start = sink.lines[i].find("hex:") + 4;
        const auto end = sink.lines[i].find(" #");
        const size_t expected = (i < 10) ? hex_bytes_per_line * 2 : (512 - 10 * hex_bytes_per_line) * 2;
        CHECK((end == std::string::npos ? sink.lines[i].size() : end) - start == expected);
    }
}

TEST_CASE("config_store_export: redaction") {
    constexpr std::array<uint16_t, 3> secrets { 0x0C44, 0x1111, 0x2222 };
    constexpr std::array<uint16_t, 2> allowed { 0x0C40, 0x0D00 };

    // Named secrets, whatever their type
    CHECK(is_redacted(0x0C44, true, secrets, allowed));
    CHECK(is_redacted(0x1111, true, secrets, allowed));
    CHECK(is_redacted(0x2222, false, secrets, allowed));

    // Strings not on the allow-list
    CHECK(is_redacted(0x0E00, true, secrets, allowed));

    // Allowed strings and non-secret values
    CHECK_FALSE(is_redacted(0x0C40, true, secrets, allowed));
    CHECK_FALSE(is_redacted(0x0D00, true, secrets, allowed));
    CHECK_FALSE(is_redacted(0x0E00, false, secrets, allowed));

    // A secret on the allow-list by mistake stays redacted
    constexpr std::array<uint16_t, 1> allowed_secret { 0x0C44 };
    CHECK(is_redacted(0x0C44, true, secrets, allowed_secret));

    CHECK(lines_of([](LineSink &s) { write_redacted(s, { 0x0C44, "WIFI AP Password" }); }) == std::vector<std::string> { "0C44 WIFI AP Password = redacted:" });
}
