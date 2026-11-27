#include <config_store/config_store_import_format.hpp>

#include <catch2/catch_test_macros.hpp>
#include <common/crc32.h>
#include <ini.h>

#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

using namespace config_store_import;
using config_store_export::Key;
using config_store_export::LineSink;

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

std::vector<std::byte> bytes_of(const auto &value) {
    const auto bytes = std::as_bytes(std::span(&value, 1));
    return { bytes.begin(), bytes.end() };
}

struct Entry {
    uint16_t id;
    std::string key;
    std::optional<Kind> kind;
    std::vector<std::byte> bytes;
};

/// Reads lines of [items] like the import: through inih, joining the continuation lines
struct Reader {
    bool ok = true;
    std::vector<Entry> entries;
    ItemAssembler assembler;

    void flush() {
        if (assembler.has_entry()) {
            entries.push_back({ assembler.id(), std::string(assembler.key()), Kind::raw, { assembler.bytes().begin(), assembler.bytes().end() } });
            assembler.clear();
        }
    }

    bool add(const char *name, const char *value) {
        const auto key = parse_key(name);
        const auto parsed = parse_value(value);
        if (!key.has_value() || !parsed.has_value()) {
            return false;
        }
        const auto chunk = std::span(parsed->bytes).first(parsed->size);
        if (key->byte_offset > 0) {
            return parsed->kind == Kind::raw && assembler.append(*key, chunk);
        }
        flush();
        if (parsed->kind == Kind::raw) {
            return assembler.start(name, *key, chunk);
        }
        entries.push_back({ key->id, name, parsed->kind, { chunk.begin(), chunk.end() } });
        return true;
    }
};

int handler(void *user, const char *, const char *name, const char *value) {
    auto &reader = *static_cast<Reader *>(user);
    if (!reader.add(name, value)) {
        reader.ok = false;
    }
    return 1;
}

Reader read_lines(const std::vector<std::string> &lines) {
    std::string text = "[items]\n";
    for (const auto &line : lines) {
        text += line + "\n";
    }
    Reader reader;
    if (ini_parse_string(text.c_str(), handler, &reader) != 0) {
        reader.ok = false;
    }
    reader.flush();
    return reader;
}

/// Writes one value with the export and reads it back
Entry round_trip(auto &&write) {
    const auto reader = read_lines(lines_of(write));
    REQUIRE(reader.ok);
    REQUIRE(reader.entries.size() == 1);
    return reader.entries[0];
}

std::string key_text(const Key &key, size_t byte_offset = 0) {
    config_store_export::KeyText text;
    std::string result(config_store_export::format_key(text, key, byte_offset));
    // inih strips trailing whitespace from the key
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

std::optional<float> parsed_float(std::string_view value) {
    const auto parsed = parse_value(value);
    if (!parsed.has_value() || parsed->kind != Kind::floating_point || parsed->size != sizeof(float)) {
        return std::nullopt;
    }
    float result;
    memcpy(&result, parsed->bytes.data(), sizeof(result));
    return result;
}

} // namespace

TEST_CASE("config_store_import: integers and bools round-trip") {
    const Key key { 0x1565, "Sound Volume" };

    const auto check_unsigned = [&](uint32_t value, size_t size) {
        const auto entry = round_trip([&](LineSink &s) { write_unsigned(s, key, value, size); });
        CHECK(entry.id == 0x1565);
        CHECK(entry.key == "1565 Sound Volume");
        CHECK(entry.kind == Kind::unsigned_integer);
        const auto expected = bytes_of(value);
        CHECK(entry.bytes == std::vector<std::byte>(expected.begin(), expected.begin() + size));
    };
    check_unsigned(0, 1);
    check_unsigned(255, 1);
    check_unsigned(65535, 2);
    check_unsigned(8514, 2);
    check_unsigned(0, 4);
    check_unsigned(4000000000u, 4);
    check_unsigned(4294967295u, 4);

    const auto check_signed = [&](int32_t value, size_t size) {
        const auto entry = round_trip([&](LineSink &s) { write_signed(s, key, value, size); });
        CHECK(entry.kind == Kind::signed_integer);
        const auto expected = bytes_of(value);
        CHECK(entry.bytes == std::vector<std::byte>(expected.begin(), expected.begin() + size));
    };
    check_signed(-128, 1);
    check_signed(127, 1);
    check_signed(-1, 1);
    check_signed(-5, 1);
    check_signed(-32768, 2);
    check_signed(32767, 2);
    check_signed(-2147483647 - 1, 4);
    check_signed(2147483647, 4);
    check_signed(-1, 4);
    check_signed(0, 4);

    for (const bool value : { false, true }) {
        const auto entry = round_trip([&](LineSink &s) { write_bool(s, key, value); });
        CHECK(entry.kind == Kind::boolean);
        CHECK(entry.bytes == std::vector<std::byte> { static_cast<std::byte>(value) });
    }
}

TEST_CASE("config_store_import: tag and size must fit the item") {
    const std::array<std::byte, 4> four {};
    const auto two = std::span(four).first(2);
    const auto one = std::span(four).first(1);

    CHECK(fits_item(Kind::unsigned_integer, two, Kind::unsigned_integer, 2));
    CHECK_FALSE(fits_item(Kind::unsigned_integer, one, Kind::unsigned_integer, 2));
    CHECK_FALSE(fits_item(Kind::unsigned_integer, four, Kind::unsigned_integer, 2));
    CHECK_FALSE(fits_item(Kind::signed_integer, two, Kind::unsigned_integer, 2));
    CHECK_FALSE(fits_item(Kind::raw, two, Kind::unsigned_integer, 2));
    CHECK(fits_item(Kind::signed_integer, four, Kind::signed_integer, 4));
    CHECK(fits_item(Kind::boolean, one, Kind::boolean, 1));
    CHECK_FALSE(fits_item(Kind::unsigned_integer, one, Kind::boolean, 1));
    CHECK(fits_item(Kind::floating_point, four, Kind::floating_point, 4));
    CHECK_FALSE(fits_item(Kind::unsigned_integer, four, Kind::floating_point, 4));
    CHECK_FALSE(fits_item(Kind::raw, four, Kind::floating_point, 4));

    // Raw values: exactly the size
    std::vector<std::byte> raw(12, std::byte { 0xAB });
    CHECK(fits_item(Kind::raw, raw, Kind::raw, 12));
    CHECK_FALSE(fits_item(Kind::raw, raw, Kind::raw, 13));
    CHECK_FALSE(fits_item(Kind::raw, raw, Kind::raw, 11));
    CHECK_FALSE(fits_item(Kind::string, raw, Kind::raw, 12));

    // Strings: text or hex, room for the terminating zero, no zero inside
    const std::array text { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' } };
    CHECK(fits_item(Kind::string, text, Kind::string, 4));
    CHECK(fits_item(Kind::raw, text, Kind::string, 4));
    CHECK_FALSE(fits_item(Kind::string, text, Kind::string, 3));
    CHECK(fits_item(Kind::string, {}, Kind::string, 1));
    const std::array with_zero { std::byte { 'a' }, std::byte { 0 }, std::byte { 'c' } };
    CHECK_FALSE(fits_item(Kind::raw, with_zero, Kind::string, 8));
    CHECK_FALSE(fits_item(Kind::unsigned_integer, one, Kind::string, 8));
}

TEST_CASE("config_store_import: floats round-trip bit-exactly") {
    const std::array values {
        0.0f,
        -0.0f,
        1.0f,
        -1.0f,
        0.4f,
        0.1f,
        1.0f / 3.0f,
        3.14159274f,
        -273.15f,
        0.0192f,
        0.000123456789f,
        1e-10f,
        123456.789f,
        999999.875f,
        1e6f,
        16777217.0f,
        FLT_MAX,
        -FLT_MAX,
        FLT_MIN,
        std::nextafter(0.0f, 1.0f),
        std::nextafter(1.0f, 2.0f),
        std::nextafter(0.4f, 0.0f),
    };
    for (const float value : values) {
        const auto entry = round_trip([&](LineSink &s) { write_float(s, { 0x0100, "Diameter" }, value); });
        CHECK(entry.kind == Kind::floating_point);
        CHECK(entry.bytes == bytes_of(value));
    }

    // Forms the firmware's own printf writes
    CHECK(std::bit_cast<uint32_t>(*parsed_float("f32:0.00000000e-308")) == std::bit_cast<uint32_t>(0.0f));
    CHECK(*parsed_float("f32:10.00000000e+05") == 1e6f);
    CHECK(*parsed_float("f32:0.99999996e-05") == 9.99999975e-06f);
    CHECK(*parsed_float("f32:0.0192000010") == 0.0192f);
    CHECK(*parsed_float("f32:0.000123457000") == 0.000123457f);
    CHECK(*parsed_float("f32:1.23456792e+08") == 123456792.0f);

    // The text is kept for comparing in the precision it was written with
    CHECK(parse_value("f32:0.400000006")->text == "0.400000006");
}

TEST_CASE("config_store_import: non-finite floats are refused") {
    CHECK_FALSE(parse_value("f32:nan"));
    CHECK_FALSE(parse_value("f32:-nan"));
    CHECK_FALSE(parse_value("f32:inf"));
    CHECK_FALSE(parse_value("f32:-inf"));
    CHECK_FALSE(parse_value("f32:infinity"));
    CHECK_FALSE(parse_value("f32:1e39"));
    CHECK_FALSE(parse_value("f32:-1e39"));
    CHECK_FALSE(parse_value("f32:0x1p3"));
    CHECK_FALSE(parse_value("f32:"));
    CHECK_FALSE(parse_value("f32:1e"));
    CHECK_FALSE(parse_value("f32:1.5 "));
    CHECK_FALSE(parse_value("f32: 1.5"));
    CHECK_FALSE(parse_value("f32:1E5"));
}

TEST_CASE("config_store_import: strings and raw values round-trip") {
    const Key key { 0x1234, "Hostname" };

    const std::array<char, 21> host { "prusa-xl_01.local" };
    auto entry = round_trip([&](LineSink &s) { write_string(s, key, host); });
    CHECK(entry.kind == Kind::string);
    CHECK(entry.bytes == bytes_of(std::array<char, 17> { 'p', 'r', 'u', 's', 'a', '-', 'x', 'l', '_', '0', '1', '.', 'l', 'o', 'c', 'a', 'l' }));

    const std::array<char, 8> empty {};
    entry = round_trip([&](LineSink &s) { write_string(s, key, empty); });
    CHECK(entry.kind == Kind::string);
    CHECK(entry.bytes.empty());

    // Text outside [A-Za-z0-9._-] goes out as hex of the text
    const std::array<char, 33> ssid { "My Wifi #1 = ok" };
    entry = round_trip([&](LineSink &s) { write_string(s, { 0x0C40, "WIFI AP SSID" }, ssid); });
    CHECK(entry.kind == Kind::raw);
    CHECK(entry.bytes == bytes_of(std::array<char, 15> { 'M', 'y', ' ', 'W', 'i', 'f', 'i', ' ', '#', '1', ' ', '=', ' ', 'o', 'k' }));

    // Raw values with a comment, and continuation lines at the chunk boundaries
    for (const size_t size : { 1u, 12u, 47u, 48u, 49u, 95u, 96u, 97u, 100u, 512u }) {
        std::vector<std::byte> data(size);
        for (size_t i = 0; i < size; i++) {
            data[i] = static_cast<std::byte>(i * 7 + 3);
        }
        const auto lines = lines_of([&](LineSink &s) { write_raw(s, { 0x0ABC, "Big" }, data, "x=1 y=2"); });
        CHECK(lines.size() == (size + config_store_export::hex_bytes_per_line - 1) / config_store_export::hex_bytes_per_line);
        const auto reader = read_lines(lines);
        REQUIRE(reader.ok);
        REQUIRE(reader.entries.size() == 1);
        CHECK(reader.entries[0].id == 0x0ABC);
        CHECK(reader.entries[0].key == "0ABC Big");
        CHECK(reader.entries[0].bytes == data);
    }

    entry = round_trip([&](LineSink &s) { config_store_export::write_redacted(s, { 0x0C44, "WIFI AP Password" }); });
    CHECK_FALSE(entry.kind.has_value());
    CHECK(entry.bytes.empty());
}

TEST_CASE("config_store_import: array index and sanitized names in the key") {
    const Key key { .id = 0x2B12, .name = "Loaded Filament", .index = 2 };
    const auto entry = round_trip([&](LineSink &s) { write_unsigned(s, key, 7, 1); });
    CHECK(entry.key == "2B12 Loaded Filament[2]");
    CHECK(entry.key == key_text(key));

    // Characters inih would split or cut on are replaced by the export
    const Key odd { 0x0001, "a=b:c#d;e[f]g" };
    const auto odd_entry = round_trip([&](LineSink &s) { write_bool(s, odd, true); });
    CHECK(odd_entry.key == "0001 a_b_c_d_e_f_g");
    CHECK(odd_entry.key == key_text(odd));
}

TEST_CASE("config_store_import: a trailing space in a name is stripped") {
    const Key key { 0x2F00, "Chamber filter time used " };
    const auto entry = round_trip([&](LineSink &s) { write_unsigned(s, key, 3600, 4); });
    CHECK(entry.key == "2F00 Chamber filter time used");
    CHECK(entry.key == key_text(key));
}

TEST_CASE("config_store_import: keys") {
    auto key = parse_key("1565 Sound Volume");
    REQUIRE(key);
    CHECK(key->id == 0x1565);
    CHECK(key->byte_offset == 0);
    CHECK(key->rest == " Sound Volume");

    key = parse_key("0ABC+48 Big");
    REQUIRE(key);
    CHECK(key->id == 0x0ABC);
    CHECK(key->byte_offset == 48);
    CHECK(key->rest == " Big");

    key = parse_key("FFFF");
    REQUIRE(key);
    CHECK(key->id == 0xFFFF);
    CHECK(key->rest.empty());

    CHECK_FALSE(parse_key(""));
    CHECK_FALSE(parse_key("156 X"));
    CHECK_FALSE(parse_key("1565X"));
    CHECK_FALSE(parse_key("abcd X"));
    CHECK_FALSE(parse_key("G565 X"));
    CHECK_FALSE(parse_key("1565+ X"));
    CHECK_FALSE(parse_key("1565+0 X"));
    CHECK_FALSE(parse_key("1565+048 X"));
    CHECK_FALSE(parse_key("1565+-48 X"));
    CHECK_FALSE(parse_key("1565+48X"));
    CHECK_FALSE(parse_key("1565+70000 X"));
}

TEST_CASE("config_store_import: refused values") {
    for (const char *value : {
             "",
             "u8",
             "u8:",
             "x8:1",
             "u64:1",
             "u:1",
             "u8:256",
             "u8:-1",
             "u8:+1",
             "u8:01",
             "u8:1.0",
             "u8: 1",
             "u16:65536",
             "u32:4294967296",
             "u32:99999999999",
             "i8:128",
             "i8:-129",
             "i8:-0",
             "i8:--1",
             "i16:32768",
             "i32:2147483648",
             "i32:-2147483649",
             "bool:2",
             "bool:",
             "bool:true",
             "hex:ABC",
             "hex:ab",
             "hex:GG",
             "hex:00 11",
             "str:a b",
             "str:a#b",
             "redacted:x",
             "BOOL:1",
         }) {
        INFO(value);
        CHECK_FALSE(parse_value(value));
    }

    CHECK_FALSE(parse_value("hex:" + std::string(2 * config_store_export::hex_bytes_per_line + 2, '0')));
    CHECK(parse_value("hex:" + std::string(2 * config_store_export::hex_bytes_per_line, '0')));
    CHECK_FALSE(parse_value("str:" + std::string(max_line_value_size + 1, 'a')));
    CHECK(parse_value("str:" + std::string(max_line_value_size, 'a')));

    CHECK(parse_value("hex:")->size == 0);
    CHECK(parse_value("str:")->size == 0);
}

TEST_CASE("config_store_import: continuation lines must continue the entry") {
    const std::string full(2 * config_store_export::hex_bytes_per_line, 'A');

    // Contiguous offsets
    CHECK(read_lines({ "0ABC Big = hex:" + full, "0ABC+48 Big = hex:" + full, "0ABC+96 Big = hex:01" }).ok);

    // Gap in the offsets
    CHECK_FALSE(read_lines({ "0ABC Big = hex:" + full, "0ABC+96 Big = hex:01" }).ok);

    // Overlap
    CHECK_FALSE(read_lines({ "0ABC Big = hex:" + full, "0ABC+48 Big = hex:" + full, "0ABC+48 Big = hex:01" }).ok);

    // Previous chunk not full
    CHECK_FALSE(read_lines({ "0ABC Big = hex:0102", "0ABC+2 Big = hex:03" }).ok);
    CHECK_FALSE(read_lines({ "0ABC Big = hex:0102", "0ABC+48 Big = hex:03" }).ok);

    // Other name or id
    CHECK_FALSE(read_lines({ "0ABC Big = hex:" + full, "0ABC+48 Small = hex:01" }).ok);
    CHECK_FALSE(read_lines({ "0ABC Big = hex:" + full, "0ABD+48 Big = hex:01" }).ok);

    // No first line, or the first line is not hex
    CHECK_FALSE(read_lines({ "0ABC+48 Big = hex:01" }).ok);
    CHECK_FALSE(read_lines({ "0ABC Big = u8:1", "0ABC+48 Big = hex:01" }).ok);
    CHECK_FALSE(read_lines({ "0ABC Big = hex:" + full, "0ABC+48 Big = u8:1" }).ok);

    // An empty continuation
    CHECK_FALSE(read_lines({ "0ABC Big = hex:" + full, "0ABC+48 Big = hex:" }).ok);

    // Longer than any entry
    std::vector<std::string> lines { "0ABC Big = hex:" + full };
    for (size_t offset = config_store_export::hex_bytes_per_line; offset < max_value_size; offset += config_store_export::hex_bytes_per_line) {
        lines.push_back("0ABC+" + std::to_string(offset) + " Big = hex:" + full);
    }
    CHECK_FALSE(read_lines(lines).ok);
    lines.pop_back();
    CHECK(read_lines(lines).ok);

    // A key longer than the export writes
    CHECK_FALSE(read_lines({ "0ABC " + std::string(config_store_export::max_key_length, 'n') + " = hex:01" }).ok);
}

TEST_CASE("config_store_import: line checker") {
    const std::vector<std::string> items { "1565 Sound Volume = u8:5", "0ABC Big = hex:0102 # note", "0001 Enabled = bool:1" };

    // The export's CRC: each line of [items] with its newline
    uint32_t export_crc = 0;
    for (const auto &line : items) {
        export_crc = crc32_sw(reinterpret_cast<const uint8_t *>(line.data()), line.size(), export_crc);
        export_crc = crc32_sw(reinterpret_cast<const uint8_t *>("\n"), 1, export_crc);
    }

    const auto check = [&](const std::string &terminator) {
        LineChecker checker;
        bool ok = true;
        for (const std::string line : { "# comment", "[backup]", "format = 1", "[items]" }) {
            ok = checker.add(line + terminator) && ok;
        }
        for (const auto &line : items) {
            ok = checker.add(line + terminator) && ok;
        }
        for (const std::string line : { "[end]", "items = 3" }) {
            ok = checker.add(line + terminator) && ok;
        }
        ok = checker.add("crc32 = 01234567") && ok;
        return std::make_tuple(ok, checker.item_line_count(), checker.item_crc());
    };

    const auto [ok, count, crc] = check("\n");
    CHECK(ok);
    CHECK(count == 3);
    CHECK(crc == export_crc);

    // CRLF: the lines read fine, but the section lines do not match and the CRC differs
    const auto [crlf_ok, crlf_count, crlf_crc] = check("\r\n");
    CHECK(crlf_ok);
    CHECK((crlf_count != 3 || crlf_crc != export_crc));

    LineChecker checker;
    CHECK(checker.add("[items]\n"));
    CHECK(checker.add("1565 Sound Volume = u8:5\r\n"));
    CHECK(checker.item_crc() != crc32_sw(reinterpret_cast<const uint8_t *>("1565 Sound Volume = u8:5\n"), 25, 0));

    // Over-long line, as fgets splits it
    CHECK_FALSE(LineChecker().add(std::string(config_store_export::max_line_length + 1, 'x') + "\n"));
    CHECK(LineChecker().add(std::string(config_store_export::max_line_length, 'x') + "\n"));
    LineChecker split;
    CHECK_FALSE(split.add(std::string(199, 'x')));

    // A line without newline is only fine at the end of the file
    LineChecker unterminated;
    CHECK(unterminated.add("crc32 = 01234567"));
    CHECK_FALSE(unterminated.add("more\n"));

    // inih would take a line starting with whitespace as a continuation of the previous value
    CHECK_FALSE(LineChecker().add(" 1565 Sound Volume = u8:5\n"));
    CHECK_FALSE(LineChecker().add("\t1565 Sound Volume = u8:5\n"));
}
