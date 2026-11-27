#include "screen_menu_home_shortcuts.hpp"

#include "home_shortcut.hpp"

#include <WindowMenuItems.hpp>
#include <config_store/store_instance.hpp>
#include <gui/menu_item/menu_item_select_menu.hpp>
#include <gui/screen/screen_menu_virtual.hpp>

namespace {

class MenuItemHomeShortcut final : public MenuItemSelectMenu {
public:
    MenuItemHomeShortcut(uint8_t slot)
        : MenuItemSelectMenu({})
        , slot_(slot) {
        SetLabel(_("Shortcut %u").formatted(label_params_, static_cast<unsigned>(slot + 1)));
        set_current_item(static_cast<int>(home_shortcut::index_of(config_store().home_shortcuts.get(slot_))));
    }

    int item_count() const final {
        return home_shortcut::actions.size();
    }

    string_view_utf8 build_item_text(int index, StringViewUtf8ParamBase &params) const final {
        return home_shortcut::label(home_shortcut::actions[index], params);
    }

protected:
    bool on_item_selected(const OnItemSelectedArgs &args) override {
        config_store().home_shortcuts.set(slot_, home_shortcut::actions[args.new_index].id);
        return true;
    }

private:
    const uint8_t slot_;
    StringViewUtf8Parameters<4> label_params_;
};

constexpr screen_menu_virtual::Configuration home_shortcuts_menu {
    .item_count = []() -> int {
        return home_shortcut::slot_count + 1; // + MI_RETURN
    },
    .item_constructor = [](WindowMenuVirtual::ItemVariant &variant, int index) {
        if (index == 0) {
            variant.emplace<MI_RETURN>();
        } else {
            variant.emplace<MenuItemHomeShortcut>(static_cast<uint8_t>(index - 1));
        } //
    },
    .title = N_("HOME SHORTCUTS"),
};

} // namespace

MI_HOME_SHORTCUTS::MI_HOME_SHORTCUTS()
    : MI_SCREEN_BASE(ScreenFactory::ScreenWithArg<ScreenMenuVirtual>(&home_shortcuts_menu), N_("Home Shortcuts")) {
}
