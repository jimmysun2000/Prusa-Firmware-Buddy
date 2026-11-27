#include <gui/user_macro.hpp>

#include <catch2/catch_test_macros.hpp>

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

} // namespace

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
