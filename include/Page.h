#pragma once

#include <lvgl.h>

class Page {
 public:
  virtual ~Page() = default;

  virtual lv_obj_t* create() = 0;
  virtual void onShow() = 0;
  virtual void update() = 0;
};
