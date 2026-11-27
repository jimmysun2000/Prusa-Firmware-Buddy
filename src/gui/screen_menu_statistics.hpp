/// @file
#pragma once

#include <MItem_menus.hpp>

class MI_STATISTICS final : public MI_SCREEN_BASE {
public:
    MI_STATISTICS();
};

/// @returns a creator for the Print Statistics screen that MI_STATISTICS opens
ScreenFactory::Creator screen_statistics_creator();
