/// @file
#pragma once

#include <i_window_menu_item.hpp>
#include <i18n.h>
#include <gui/menu_item/menu_item_toggle_switch.hpp>

// The run() functions do what a click does, also from outside the Filament menu. Where only one tool fits,
// no tool list is shown and the question passed to run(), if any, is asked Yes/No with No focused instead.

class MI_LOAD : public IWindowMenuItem {
    constexpr static const char *const label = N_("Load Filament");
    constexpr static const char *const warning_loaded = N_("Filament appears to be already loaded, are you sure you want to load it anyway?");

public:
    MI_LOAD();
    virtual void click(IWindowMenu &) override;

    /// Lets the user choose the tool, picks it and loads filament
    static void run(const char *question = nullptr);
};

class MI_UNLOAD : public IWindowMenuItem {
    constexpr static const char *const label = N_("Unload Filament");

public:
    MI_UNLOAD();
    virtual void click(IWindowMenu &) override;

    /// Lets the user choose the tool, picks it and unloads the filament
    static void run(const char *question = nullptr);
};

class MI_CHANGE : public IWindowMenuItem {
    constexpr static const char *const label = N_("Change Filament");

public:
    MI_CHANGE();
    virtual void click(IWindowMenu &) override;

    /// Lets the user choose a tool with filament, picks it and changes the filament
    static void run(const char *question = nullptr);
};

class MI_CHANGEALL : public IWindowMenuItem {
    constexpr static const char *const label = N_("Change All");

public:
    MI_CHANGEALL();

    /// Opens the screen to choose the change of each tool
    static void run();

protected:
    virtual void click(IWindowMenu &) override;
};

class MI_LOAD_ALL : public IWindowMenuItem {
    constexpr static const char *const label = N_("Load All");

public:
    MI_LOAD_ALL();

    /// Opens the screen to choose the change of each tool, starting with the filament to load into all
    static void run();

protected:
    virtual void click(IWindowMenu &) override;
};

class MI_UNLOAD_ALL : public IWindowMenuItem {
    constexpr static const char *const label = N_("Unload All");

public:
    MI_UNLOAD_ALL();

    /// Opens the screen to choose the change of each tool, with every loaded tool set to unload
    static void run();

protected:
    virtual void click(IWindowMenu &) override;
};

/// Runs the auto retract on every loaded tool (M1705 A)
class MI_RETRACT_ALL : public IWindowMenuItem {
    constexpr static const char *const label = N_("Retract All");

public:
    constexpr static const char *const question = N_("Retract the filament from the nozzle of every loaded tool? The tools heat up together and end parked. Tools already retracted or with flexible filament are skipped.");

    MI_RETRACT_ALL();

    /// Whether a tool has filament loaded and Auto Retract is on; the item is disabled otherwise
    static bool available();

    /// Starts the retract, without a question
    static void run();

protected:
    virtual void click(IWindowMenu &) override;
};

class MI_PURGE : public IWindowMenuItem {
    constexpr static const char *const label = N_("Purge Filament");

public:
    MI_PURGE();
    virtual void click(IWindowMenu &) override;

    /// Lets the user choose a tool with filament, picks it and purges
    static void run(const char *question = nullptr);
};

#if PRINTER_IS_PRUSA_XL()
/// Runs M1706 on a tool without loaded filament
class MI_SHAPE_TIP : public IWindowMenuItem {
    constexpr static const char *const label = N_("Shape Filament Tip");

public:
    MI_SHAPE_TIP();
    virtual void click(IWindowMenu &) override;

    /// Lets the user choose a tool without filament, picks it and starts M1706
    static void run(const char *question = nullptr);
};

/// Runs M1706 C on a tool without loaded filament
class MI_CLEANING_FILAMENT : public IWindowMenuItem {
    constexpr static const char *const label = N_("Cleaning Filament");

public:
    MI_CLEANING_FILAMENT();
    virtual void click(IWindowMenu &) override;

    /// Lets the user choose a tool without filament, picks it and starts M1706 C
    static void run(const char *question = nullptr);
};
#endif

class MI_ASK_COLOR_ON_LOAD final : public MenuItemToggleSwitch {
public:
    MI_ASK_COLOR_ON_LOAD();

protected:
    void toggled([[maybe_unused]] Tristate previous_value) override;
};
