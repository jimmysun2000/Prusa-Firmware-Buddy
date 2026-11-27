#include "screen_menu_macros.hpp"

#include "user_macro.hpp"

#include <ScreenHandler.hpp>
#include <WindowMenuInfo.hpp>
#include <WindowMenuItems.hpp>
#include <dialog_text_input.hpp>
#include <gui/menu_item/menu_item_select_menu.hpp>
#include <gui/screen/screen_menu_virtual.hpp>

#include <array>
#include <string_view>
#include <utility>

namespace {

class MenuItemMacro final : public IWindowMenuItem {
public:
    MenuItemMacro(uint8_t n)
        : IWindowMenuItem(string_view_utf8 {}, nullptr, user_macro::file_exists(n) ? is_enabled_t::yes : is_enabled_t::no)
        , n_(n) {
        SetLabel(user_macro::label(n, label_params_));
    }

protected:
    void click(IWindowMenu &) override {
        user_macro::run(n_);
    }

private:
    const uint8_t n_;
    StringViewUtf8Parameters<user_macro::label_buffer_size> label_params_;
};

class MenuItemMacroName final : public WiInfo<user_macro::label_buffer_size> {
public:
    MenuItemMacroName(uint8_t n)
        : WiInfo(_("Name"))
        , n_(n) {
        update();
    }

protected:
    void click(IWindowMenu &) override {
        user_macro::Name name = user_macro::name(n_);
        if (DialogTextInput::exec(_("Name"), name)) {
            user_macro::set_name(n_, name);
            update();
        }
    }

private:
    /// Copies the label into the row, which redraws when the bytes change
    void update() {
        StringViewUtf8Parameters<user_macro::label_buffer_size> params;
        ChangeInformation(user_macro::label(n_, params));
    }

    const uint8_t n_;
};

class MenuItemMacroFile final : public WiInfo<user_macro::label_buffer_size> {
public:
    MenuItemMacroFile(uint8_t n)
        : WiInfo(_("File"), nullptr, is_enabled_t::no) {
        const user_macro::Path path = user_macro::file_path(n);
        const std::string_view text = path.data();
        ChangeInformation(text.substr(text.rfind('/') + 1));
    }
};

constexpr int macro_settings_item_count = 3; // MI_RETURN, Name, File

void setup_macro_settings_item(WindowMenuVirtual::ItemVariant &variant, int index, uint8_t n) {
    switch (index) {
    case 0:
        variant.emplace<MI_RETURN>();
        break;
    case 1:
        variant.emplace<MenuItemMacroName>(n);
        break;
    default:
        variant.emplace<MenuItemMacroFile>(n);
        break;
    }
}

/// The settings screen of macro @p n; one configuration per macro, as a configuration carries no argument
template <size_t n>
constexpr screen_menu_virtual::Configuration macro_settings_menu {
    .item_count = []() -> int {
        return macro_settings_item_count;
    },
    .item_constructor = [](WindowMenuVirtual::ItemVariant &variant, int index) { setup_macro_settings_item(variant, index, static_cast<uint8_t>(n)); },
    .title = N_("MACRO SETTINGS"),
};

constexpr auto macro_settings_menus = []<size_t... i>(std::index_sequence<i...>) {
    return std::array { &macro_settings_menu<i + 1>... };
}(std::make_index_sequence<user_macro::count>());

/// Pops up the macros to choose the one whose settings to open
class MenuItemMacroSettings final : public MenuItemSelectMenu {
public:
    MenuItemMacroSettings()
        : MenuItemSelectMenu(_("Macro Settings")) {
        set_behavior(Behavior::select_only);
    }

    int item_count() const final {
        return user_macro::count;
    }

    string_view_utf8 build_item_text(int index, StringViewUtf8ParamBase &params) const final {
        return user_macro::label(static_cast<uint8_t>(index + 1), params);
    }

protected:
    bool on_item_selected(const OnItemSelectedArgs &args) override {
        Screens::Access()->Open(ScreenFactory::ScreenWithArg<ScreenMenuVirtual>(macro_settings_menus[args.new_index]));
        return true;
    }
};

constexpr screen_menu_virtual::Configuration macros_menu {
    .item_count = []() -> int {
        return user_macro::count + 2; // + MI_RETURN + MenuItemMacroSettings
    },
    .item_constructor = [](WindowMenuVirtual::ItemVariant &variant, int index) {
        if (index == 0) {
            variant.emplace<MI_RETURN>();
        } else if (index <= user_macro::count) {
            variant.emplace<MenuItemMacro>(static_cast<uint8_t>(index));
        } else {
            variant.emplace<MenuItemMacroSettings>();
        } //
    },
    .title = N_("MACROS"),
};

} // namespace

MI_MACROS::MI_MACROS()
    : MI_SCREEN_BASE(ScreenFactory::ScreenWithArg<ScreenMenuVirtual>(&macros_menu), N_("Macros")) {
}
