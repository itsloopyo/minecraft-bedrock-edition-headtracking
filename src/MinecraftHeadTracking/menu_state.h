#pragma once

#include <windows.h>

namespace mcht::ui {

// True while the game is showing a menu rather than gameplay: the pause menu,
// the inventory, the chat box, or any other screen you interact with.
//
// Read from the OS cursor rather than from the game's screen stack. Bedrock
// captures and hides the pointer during gameplay and releases it for every
// screen that has something to click, so CURSOR_SHOWING is the same signal the
// player sees. It also needs no per-build offsets, which matters here: every
// address this mod pins has to be rederived after a Minecraft patch, and a
// menu check that survives patches for free is worth more than a tidier one
// that does not.
//
// Caveat worth knowing before trusting this on a gamepad: Bedrock draws its
// own virtual cursor for controller input and leaves the OS pointer hidden, so
// on a controller this reads "gameplay" inside menus. Head tracking staying
// live in a menu is cosmetic rather than an advantage, so that failure
// direction is the acceptable one - unlike the PvP gate, which must never fail
// open.
inline bool MenuIsOpen() {
    CURSORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info)) {
        // Nothing readable means no evidence of a menu. Suspending tracking on
        // a failed query would freeze the view for the rest of the session.
        return false;
    }
    return (info.flags & CURSOR_SHOWING) != 0;
}

}  // namespace mcht::ui
