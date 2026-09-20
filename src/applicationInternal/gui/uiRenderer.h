#pragma once

#include <lvgl.h>

#include "applicationInternal/storage/configUi.h"

/*
  Turns a screen out of ui.json into LVGL objects.

  The one rule: this knows about widgets and grids, and nothing else. It does
  not read files, does not register anything, does not decide when a screen is
  built. That keeps it testable against a headless LVGL - which matters, because
  a renderer nobody can test is a renderer nobody can change.

  ## Binding

  Every widget names a command; the name is resolved to an id once, here, and
  kept in the object's user data. A widget whose command the device does not
  have is still drawn, but shown as disabled: a file moved from another remote
  is the normal case, and a screen that silently loses half its buttons is
  harder to diagnose than one with greyed-out ones.

  ## Memory

  guiMemoryOptimizer keeps three tabs alive and deletes the rest. Everything
  here hangs off the parent it is given, so deleting that parent takes all of
  it with it - no bookkeeping of our own, nothing to leak.
*/

namespace uiRenderer {

struct Result {
  uint16_t widgetsDrawn = 0;
  uint16_t widgetsDisabled = 0; // command unknown on this device
};

/*
  Draws the screen into parent. The parent gets a grid layout; anything already
  in it stays where it is.
*/
Result render(const configModel::Screen &screen, lv_obj_t *parent);

} // namespace uiRenderer
