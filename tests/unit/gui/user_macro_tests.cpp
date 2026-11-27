#include <gui/user_macro.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

using namespace user_macro;

namespace {

Fault check(std::string_view content) {
    FileCheck check;
    check.feed(content);
    return check.fault();
}

/// Lines of @p line_length characters, each ended by @p line_end, @p size bytes in total
std::string file_of(size_t size, size_t line_length, std::string_view line_end = "\n") {
    std::string content;
    while (content.size() < size) {
        content.append(line_length, 'G');
        content.append(line_end);
    }
    content.resize(size);
    return content;
}

/// A name with the bytes of @p text, the rest filled with @p fill
Name name_of(std::string_view text, char fill = '\0') {
    Name name;
    name.fill(fill);
    std::copy(text.begin(), text.end(), name.begin());
    return name;
}

std::string text_of(const Name &name) {
    return std::string(name.data());
}

} // namespace

TEST_CASE("user_macro::trimmed") {
    CHECK(text_of(trimmed(name_of("Park"))) == "Park");
    CHECK(text_of(trimmed(name_of("  Park  "))) == "Park");
    CHECK(text_of(trimmed(name_of("Purge T1"))) == "Purge T1");
    CHECK(text_of(trimmed(name_of(" a  b "))) == "a  b");
    CHECK(text_of(trimmed(name_of(""))) == "");
    CHECK(text_of(trimmed(name_of("   "))) == "");
    CHECK(text_of(trimmed(name_of(std::string(max_name_length, ' ')))) == "");
    CHECK(text_of(trimmed(name_of(std::string(max_name_length, 'W')))) == std::string(max_name_length, 'W'));

    SECTION("zero-padded") {
        const Name name = trimmed(name_of("  Park"));
        CHECK(std::all_of(name.begin() + 4, name.end(), [](char c) { return c == '\0'; }));
    }

    SECTION("only up to the terminator") {
        Name name = name_of("ab");
        name[5] = 'x';
        CHECK(text_of(trimmed(name)) == "ab");
        CHECK(trimmed(name)[5] == '\0');
    }
}

TEST_CASE("user_macro::sanitized") {
    CHECK(text_of(sanitized(name_of("Park 100%"))) == "Park 100%");
    CHECK(text_of(sanitized(name_of(" !~{}"))) == " !~{}");
    CHECK(text_of(sanitized(name_of("a\tb\x7f"))) == "a?b?");
    CHECK(text_of(sanitized(name_of("\xc3\xa9"))) == "??");

    SECTION("a missing terminator is forced") {
        const Name name = sanitized(name_of("", 'W'));
        CHECK(name.back() == '\0');
        CHECK(text_of(name) == std::string(max_name_length, 'W'));
    }

    SECTION("bytes after the terminator stay") {
        Name name = name_of("ab");
        name[5] = '\x01';
        CHECK(sanitized(name)[5] == '\x01');
    }
}

TEST_CASE("user_macro::icons") {
    SECTION("stored values keep their meaning") {
        CHECK(static_cast<uint8_t>(Icon::play) == 0);
        CHECK(static_cast<uint8_t>(Icon::home) == 1);
        CHECK(static_cast<uint8_t>(Icon::stop) == 2);
        CHECK(static_cast<uint8_t>(Icon::pause) == 3);
        CHECK(static_cast<uint8_t>(Icon::check) == 4);
        CHECK(static_cast<uint8_t>(Icon::cross) == 5);
        CHECK(static_cast<uint8_t>(Icon::repeat) == 6);
        CHECK(static_cast<uint8_t>(Icon::back) == 7);
        CHECK(static_cast<uint8_t>(Icon::branch) == 8);
        CHECK(static_cast<uint8_t>(Icon::spools) == 9);
    }

    SECTION("each icon is offered once") {
        std::set<uint8_t> values;
        for (const Icon icon : icons) {
            CHECK(values.insert(static_cast<uint8_t>(icon)).second);
        }
        CHECK(values.size() == 10);
    }

    SECTION("the store default 0 is Play") {
        CHECK(icons[icon_index_of(0)] == Icon::play);
    }

    SECTION("an unknown id is Play") {
        for (unsigned id = 10; id <= 255; ++id) {
            CHECK(icons[icon_index_of(static_cast<uint8_t>(id))] == Icon::play);
        }
    }

    SECTION("icon_index_of finds each icon") {
        for (size_t i = 0; i < icons.size(); ++i) {
            CHECK(icon_index_of(static_cast<uint8_t>(icons[i])) == i);
        }
    }
}

TEST_CASE("user_macro::FileCheck file size") {
    CHECK(check("") == Fault::none);
    CHECK(check(file_of(max_file_size, 40)) == Fault::none);
    CHECK(check(file_of(max_file_size + 1, 40)) == Fault::file_too_long);
    CHECK(check(std::string(max_file_size, '\n')) == Fault::none);
    CHECK(check(std::string(max_file_size + 1, '\n')) == Fault::file_too_long);
}

TEST_CASE("user_macro::FileCheck line length") {
    CHECK(check(std::string(max_line_length, 'G')) == Fault::none);
    CHECK(check(std::string(max_line_length + 1, 'G')) == Fault::line_too_long);
    CHECK(check(file_of(max_file_size, max_line_length)) == Fault::none);
    CHECK(check(file_of(max_file_size, max_line_length + 1)) == Fault::line_too_long);

    SECTION("CR, LF and CRLF all end a line") {
        CHECK(check(file_of(500, max_line_length, "\r")) == Fault::none);
        CHECK(check(file_of(500, max_line_length, "\r\n")) == Fault::none);
    }

    SECTION("the whole raw line counts, comments and spaces included") {
        const std::string line = "G1 X10 ; " + std::string(max_line_length, ' ');
        CHECK(check("G28\n" + line + "\nG28\n") == Fault::line_too_long);
    }
}

TEST_CASE("user_macro::FileCheck fed in parts") {
    SECTION("a line across parts") {
        FileCheck check;
        check.feed(std::string(50, 'G'));
        CHECK(check.fault() == Fault::none);
        check.feed(std::string(max_line_length - 50, 'G'));
        CHECK(check.fault() == Fault::none);
        check.feed("G");
        CHECK(check.fault() == Fault::line_too_long);
    }

    SECTION("the size across parts") {
        FileCheck check;
        check.feed(file_of(max_file_size - 10, 20));
        check.feed(std::string(10, '\n'));
        CHECK(check.fault() == Fault::none);
        check.feed("\n");
        CHECK(check.fault() == Fault::file_too_long);
    }

    SECTION("the first fault stays") {
        FileCheck check;
        check.feed(std::string(max_line_length + 1, 'G'));
        check.feed(std::string(max_file_size, '\n'));
        CHECK(check.fault() == Fault::line_too_long);
    }
}
