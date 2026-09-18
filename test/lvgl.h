#pragma once
/*
  Minimal LVGL stub for native unit tests.

  Several OMOTE headers (guiBase.h, guiRegistry.h, guiMemoryOptimizer.h, ...)
  include <lvgl.h> although the modules under test never touch LVGL itself.
  In env:native_test this file is found first (via -I test) so the headers
  compile without pulling in the real LVGL library.

  Only declarations are needed here - none of these types is ever
  instantiated or dereferenced by the code under test.
*/

#include <stdint.h>
#include <stddef.h>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;
typedef struct _lv_style_t { int unused; } lv_style_t;
typedef struct _lv_color_t { uint32_t full; } lv_color_t;
typedef struct _lv_img_dsc_t { const void *data; } lv_img_dsc_t;

typedef enum { LV_ANIM_OFF = 0, LV_ANIM_ON = 1 } lv_anim_enable_t;

#define LV_IMG_DECLARE(var_name) extern const lv_img_dsc_t var_name;
