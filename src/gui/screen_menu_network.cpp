/**
 * @file screen_menu_network.cpp
 */
#include "screen_menu_network.hpp"
#include "printers.h"
#include "wui_api.h"
#include "netdev.h"
#include <http_lifetime.h>

ScreenMenuNetwork::ScreenMenuNetwork()
    : ScreenMenuNetwork__(_(label)) {
#if HAS_EMBEDDED_ESP32()
    update_wifi_items();
#endif
}

#if HAS_EMBEDDED_ESP32()
void ScreenMenuNetwork::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    if (event == GUI_event_t::LOOP) {
        update_wifi_items();
    }

    ScreenMenuNetwork__::windowEvent(sender, event, param);
}

void ScreenMenuNetwork::update_wifi_items() {
    // The Wi-Fi settings are locked too: their DHCP/static switch silently turns the Wi-Fi
    // back on and their setup wizard can't connect anyway.
    const bool wifi_usable = netdev_is_wifi_usable();
    if (!wifi_usable) {
        Item<MI_NET_INTERFACE_t>().set_current_item(static_cast<int>(NETDEV_ETH_ID));
    }
    Item<MI_NET_INTERFACE_t>().set_enabled(wifi_usable);
    Item<MI_WIFI_SETTINGS>().set_enabled(wifi_usable);
}
#endif
