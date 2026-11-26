/**
 * @file screen_menu_network.hpp
 */

#pragma once

#include "screen_menu.hpp"
#include "WindowMenuItems.hpp"
#include "MItem_menus.hpp"
#include "MItem_tools.hpp"
#include "MItem_network.hpp"
#include <option/buddy_enable_connect.h>
#include <option/has_esp.h>
#include <option/has_embedded_esp32.h>

using ScreenMenuNetwork__ = ScreenMenu<EFooter::Off,
    MI_RETURN,
#if HAS_EMBEDDED_ESP32()
    MI_WIFI_ENABLE,
#endif
#if HAS_ESP()
    MI_NET_INTERFACE_t,
#endif
    MI_NETWORK_STATUS,
#if HAS_ESP()
    MI_WIFI_SETTINGS,
#endif
    MI_ETH_SETTINGS,
#if BUDDY_ENABLE_CONNECT()
    MI_PRUSA_CONNECT,
#endif
#if !PRINTER_IS_PRUSA_iX()
    MI_PRUSALINK,
#endif
    MI_METRICS_SETTINGS>;

class ScreenMenuNetwork : public ScreenMenuNetwork__ {
public:
    constexpr static const char *label = N_("NETWORK");

    ScreenMenuNetwork();

#if HAS_EMBEDDED_ESP32()
protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    /// Locks the items that need the Wi-Fi while it can't be used
    void update_wifi_items();
#endif
};
