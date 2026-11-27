#include <enclosure_renderer.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using namespace json;

namespace {

bool enclosure_enabled;
std::optional<float> enclosure_temperature;
uint16_t enclosure_fan_rpm;

void fill_enclosure() {
    enclosure_enabled = true;
    enclosure_temperature = 31.54f;
    enclosure_fan_rpm = 1450;
}

/// The renderer the route gets, without its HTTP part
class EnclosureRenderer final : public LowLevelJsonRenderer {
protected:
    JsonResult content(size_t resume_point, JsonOutput &output) override {
        return nhttp::handler::render_enclosure(resume_point, output);
    }
};

std::string render(size_t chunk_size) {
    EnclosureRenderer renderer;
    std::string response;
    auto result = JsonResult::Incomplete;
    while (result == JsonResult::Incomplete) {
        std::string buffer(chunk_size, '\0');
        const auto [result_partial, written] = renderer.render(reinterpret_cast<uint8_t *>(buffer.data()), chunk_size);
        REQUIRE(written <= chunk_size);
        REQUIRE((written > 0 || result_partial == JsonResult::Complete));
        response.append(buffer.data(), written);
        result = result_partial;
    }
    REQUIRE(result == JsonResult::Complete);
    return response;
}

} // namespace

namespace nhttp::handler::enclosure_data {

bool enabled() {
    return enclosure_enabled;
}

std::optional<float> temperature() {
    return enclosure_temperature;
}

uint16_t fan_rpm() {
    return enclosure_fan_rpm;
}

} // namespace nhttp::handler::enclosure_data

TEST_CASE("Enclosure JSON - one buffer") {
    fill_enclosure();
    CHECK(render(4096) == "{\"enabled\":true,\"temp\":31.5,\"fan_rpm\":1450}");
}

TEST_CASE("Enclosure JSON - resumed in small chunks") {
    fill_enclosure();
    // 15 is the smallest chunk that fits the longest fields ("enabled":true or "fan_rpm":1450, and the
    // terminating zero)
    for (size_t chunk : { 15, 16, 20, 30 }) {
        CAPTURE(chunk);
        CHECK(render(chunk) == "{\"enabled\":true,\"temp\":31.5,\"fan_rpm\":1450}");
    }
}

TEST_CASE("Enclosure JSON - no temperature estimate") {
    fill_enclosure();
    enclosure_enabled = false;
    enclosure_temperature = std::nullopt;
    enclosure_fan_rpm = 0;
    CHECK(render(4096) == "{\"enabled\":false,\"temp\":null,\"fan_rpm\":0}");

    enclosure_temperature = NAN;
    CHECK(render(4096) == "{\"enabled\":false,\"temp\":null,\"fan_rpm\":0}");
}

TEST_CASE("Enclosure JSON - estimate ends between chunks") {
    fill_enclosure();
    EnclosureRenderer renderer;
    std::string response;
    bool changed = false;
    auto result = JsonResult::Incomplete;
    while (result == JsonResult::Incomplete) {
        uint8_t buffer[16];
        const auto [result_partial, written] = renderer.render(buffer, sizeof(buffer));
        response.append(reinterpret_cast<const char *>(buffer), written);
        result = result_partial;
        // The temperature is read when it is output, so a value that disappears in between is not used
        if (!changed) {
            REQUIRE(response.find("\"temp\"") == std::string::npos);
            enclosure_temperature = std::nullopt;
            changed = true;
        }
    }
    REQUIRE(result == JsonResult::Complete);
    CHECK(response == "{\"enabled\":true,\"temp\":null,\"fan_rpm\":1450}");
}
