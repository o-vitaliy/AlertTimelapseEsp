#pragma once

#include "Page.h"

class NoWifiPage : public Page {
 public:
  explicit NoWifiPage(const char* accessPointName);

  lv_obj_t* create() override;
  void onShow() override;
  void update() override;
  void showConnecting();
  void showAccessPointInstructions();

 private:
  const char* accessPointName_;
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* title_ = nullptr;
  lv_obj_t* instructions_ = nullptr;
  lv_obj_t* accessPointLabel_ = nullptr;
  lv_obj_t* details_ = nullptr;
};
