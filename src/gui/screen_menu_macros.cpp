#include "screen_menu_macros.hpp"

#include "user_macro.hpp"

#include <WindowMenuItems.hpp>
#include <gui/screen/screen_menu_virtual.hpp>

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
    StringViewUtf8Parameters<4> label_params_;
};

constexpr screen_menu_virtual::Configuration macros_menu {
    .item_count = []() -> int {
        return user_macro::count + 1; // + MI_RETURN
    },
    .item_constructor = [](WindowMenuVirtual::ItemVariant &variant, int index) {
        if (index == 0) {
            variant.emplace<MI_RETURN>();
        } else {
            variant.emplace<MenuItemMacro>(static_cast<uint8_t>(index));
        } //
    },
    .title = N_("MACROS"),
};

} // namespace

MI_MACROS::MI_MACROS()
    : MI_SCREEN_BASE(ScreenFactory::ScreenWithArg<ScreenMenuVirtual>(&macros_menu), N_("Macros")) {
}
