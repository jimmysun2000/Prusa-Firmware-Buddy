#include <gui/home_shortcut.hpp>
#include <gui/user_macro.hpp>

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace home_shortcut;

TEST_CASE("home_shortcut::actions") {
    SECTION("IDs are unique") {
        std::set<uint8_t> ids;
        for (const Action &action : actions) {
            CHECK(ids.insert(action.id).second);
        }
    }

    SECTION("stored IDs keep their meaning") {
        CHECK(find(0).kind == Kind::none);
        CHECK(find(1).kind == Kind::auto_home);
        CHECK(find(2).kind == Kind::pick_park);
        CHECK(find(3).kind == Kind::move_axis);
        CHECK(find(4).kind == Kind::motors_off);
        CHECK(find(5).kind == Kind::wipe_nozzle);
        for (uint8_t n = 1; n <= user_macro::count; ++n) {
            CHECK(find(15 + n).kind == Kind::macro);
            CHECK(find(15 + n).macro == n);
        }
    }

    SECTION("each macro has one entry") {
        std::set<uint8_t> macros;
        for (const Action &action : actions) {
            if (action.kind == Kind::macro) {
                CHECK(action.macro >= 1);
                CHECK(action.macro <= user_macro::count);
                CHECK(macros.insert(action.macro).second);
            } else {
                CHECK(action.macro == 0);
            }
        }
        CHECK(macros.size() == user_macro::count);
    }

    SECTION("the config store default 0 is None") {
        CHECK(find(0).kind == Kind::none);
        CHECK(index_of(0) == 0);
    }

    SECTION("an unknown ID is None") {
        for (unsigned id = 0; id <= 255; ++id) {
            const Action &action = find(static_cast<uint8_t>(id));
            if (action.id != id) {
                CHECK(action.kind == Kind::none);
                CHECK(index_of(static_cast<uint8_t>(id)) == 0);
            }
        }
        CHECK(find(6).kind == Kind::none);
        CHECK(find(15).kind == Kind::none);
        CHECK(find(24).kind == Kind::none);
        CHECK(find(255).kind == Kind::none);
    }

    SECTION("index_of and find agree") {
        for (size_t i = 0; i < actions.size(); ++i) {
            CHECK(index_of(actions[i].id) == i);
            CHECK(&find(actions[i].id) == &actions[i]);
        }
    }
}
