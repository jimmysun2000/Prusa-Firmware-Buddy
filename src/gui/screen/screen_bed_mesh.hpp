/// @file
#pragma once

#include <screen.hpp>
#include <window_header.hpp>

#include <cstdint>

/// Colour map of the bed mesh with its statistics. Display only.
class WindowBedMesh final : public window_t {
public:
    WindowBedMesh(window_t *parent, Rect16 rect);

    /// Redraws if the mesh or its status changed since the last check
    void update();

    enum class Status : uint8_t {
        no_mesh,
        probing,
        incomplete,
        complete,
    };

protected:
    void unconditionalDraw() override;

private:
    static Status read_status();

    uint32_t revision_;
    Status status_;
};

/// Shows the bed leveling mesh (from the last probing since boot).
class ScreenBedMesh final : public screen_t {
public:
    /// @param auto_opened opened by the printer after probing rather than from a menu; closes itself after a while
    explicit ScreenBedMesh(bool auto_opened = false);

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    window_header_t header;
    WindowBedMesh mesh;

    uint32_t opened_ms;
    uint32_t last_update_ms;
    bool auto_opened;
};

/// Opens ScreenBedMesh once per print job, shortly after the mesh probing of that print finished,
/// if the user enabled it. Nothing waits on the screen; the print goes on.
///
/// To be called periodically by the print screen while it is the current screen.
/// @param printing the print is running (not paused, stopped, finished...)
void bed_mesh_popup_after_probing(bool printing);
