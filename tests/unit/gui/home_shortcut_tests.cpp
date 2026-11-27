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

    SECTION("stored screen IDs keep their meaning") {
        CHECK(find(32).kind == Kind::temperature);
        CHECK(find(33).kind == Kind::move_z);
        CHECK(find(34).kind == Kind::bed_mesh);
        CHECK(find(35).kind == Kind::bed_tiles);
        CHECK(find(36).kind == Kind::tools_overview);
        CHECK(find(37).kind == Kind::statistics);
        CHECK(find(38).kind == Kind::maintenance);
        CHECK(find(39).kind == Kind::sensor_info);
        CHECK(find(40).kind == Kind::network_status);
        CHECK(find(41).kind == Kind::prusalink);
        CHECK(find(42).kind == Kind::lights);
        CHECK(find(43).kind == Kind::enclosure);
        CHECK(find(44).kind == Kind::wiper_setup);
        CHECK(find(45).kind == Kind::calibrations);
        CHECK(find(46).kind == Kind::filament_sensors);
        CHECK(find(47).kind == Kind::macros_screen);
        CHECK(find(48).kind == Kind::load_all);
        CHECK(find(49).kind == Kind::unload_all);
        CHECK(find(50).kind == Kind::change_all);
        CHECK(find(51).kind == Kind::tool_settings);
        CHECK(find(52).kind == Kind::input_shaper);
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
        CHECK(find(31).kind == Kind::none);
        CHECK(find(53).kind == Kind::none);
        CHECK(find(255).kind == Kind::none);
    }

    SECTION("index_of and find agree") {
        for (size_t i = 0; i < actions.size(); ++i) {
            CHECK(index_of(actions[i].id) == i);
            CHECK(&find(actions[i].id) == &actions[i]);
        }
    }
}
