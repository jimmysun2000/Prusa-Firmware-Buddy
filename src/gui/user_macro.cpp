#include "user_macro.hpp"

#include <common/marlin_vars.hpp>
#include <config_store/store_instance.hpp>
#include <gcode/gcode_buffer.hpp>
#include <gcode/gcode_loader.hpp>
#include <gcode/inject_queue_actions.hpp>
#include <i18n.h>
#include <marlin_client.hpp>
#include <unique_file_ptr.hpp>
#include <utils/string_builder.hpp>
#include <window_msgbox.hpp>

#include <optional>
#include <sys/unistd.h>
#include <tuple>
#include <type_traits>

static_assert(user_macro::max_file_size == GCodeLoader::gcode_stream_buffer_size - 1);
static_assert(user_macro::max_line_length == std::tuple_size_v<GcodeBuffer::Container> - 1);

static_assert(std::is_same_v<decltype(config_store().macro_names)::value_type, user_macro::Name>);
static_assert(decltype(config_store().macro_names)::item_count == user_macro::count);

// The select menu and its popup format the labels into 16-byte parameter buffers
static_assert(user_macro::label_buffer_size <= 16);

namespace user_macro {

namespace {

    constexpr const char *printer_busy_text = N_("Printer is busy. Please try repeating the action later.");

    /// @returns nullopt if the file cannot be read
    std::optional<Fault> check_file(const char *path) {
        unique_file_ptr file { fopen(path, "rb") };
        if (!file) {
            return std::nullopt;
        }

        FileCheck check;
        std::array<char, 64> buffer;
        while (check.fault() == Fault::none) {
            const size_t read = fread(buffer.data(), 1, buffer.size(), file.get());
            if (read == 0) {
                break;
            }
            check.feed({ buffer.data(), read });
        }

        if (ferror(file.get())) {
            return std::nullopt;
        }
        return check.fault();
    }

    void show_busy() {
        MsgBoxWarning(_(printer_busy_text), Responses_Ok);
    }

} // namespace

Name name(uint8_t n) {
    return trimmed(sanitized(config_store().macro_names.get(n - 1)));
}

void set_name(uint8_t n, const Name &name) {
    config_store().macro_names.set(n - 1, trimmed(sanitized(name)));
}

string_view_utf8 label(uint8_t n, StringViewUtf8ParamBase &params, size_t max_length) {
    const Name macro_name = name(n);
    const std::string_view text = std::string_view(macro_name.data()).substr(0, max_length);
    if (text.empty()) {
        return _("Macro %u").formatted(params, static_cast<unsigned>(n));
    }
    // The name is a parameter, never the format
    return string_view_utf8::MakeCPUFLASH("%.*s").formatted(params, text);
}

Path file_path(uint8_t n) {
    // The inject queue loader opens the same path for GCodeMacroButton
    Path path;
    StringBuilder sb(path);
    sb.append_printf("/usb/macros/btn_%u.gcode", static_cast<unsigned>(n));
    return path;
}

bool file_exists(uint8_t n) {
    return access(file_path(n).data(), R_OK) == 0;
}

void run(uint8_t n, bool confirm) {
    if (!marlin_client::is_idle() || marlin_vars().is_processing.get()) {
        show_busy();
        return;
    }

    const Path path = file_path(n);
    StringViewUtf8Parameters<48> params;
    const std::optional<Fault> fault = check_file(path.data());
    if (!fault.has_value()) {
        MsgBoxWarning(_("The macro file %s cannot be read.").formatted(params, path.data()), Responses_Ok);
        return;
    }

    switch (*fault) {
    case Fault::none:
        break;
    case Fault::file_too_long:
        MsgBoxWarning(_("The macro file %s is too long. A macro file can have at most %u bytes.").formatted(params, path.data(), static_cast<unsigned>(max_file_size)), Responses_Ok);
        return;
    case Fault::line_too_long:
        MsgBoxWarning(_("The macro file %s has a line that is too long. A line can have at most %u characters.").formatted(params, path.data(), static_cast<unsigned>(max_line_length)), Responses_Ok);
        return;
    }

    if (confirm) {
        StringViewUtf8Parameters<label_buffer_size> label_params;
        std::array<char, label_buffer_size> label_text;
        label(n, label_params).copyToRAM(label_text.data(), label_text.size());
        if (MsgBoxQuestion(_("Run %s?").formatted(params, label_text.data()), Responses_YesNo, 1) != Response::Yes) {
            return;
        }
    }

    // The server refuses it too if the printer stopped being idle meanwhile
    if (!marlin_client::inject(GCodeMacroButton { n })) {
        show_busy();
    }
}

} // namespace user_macro
