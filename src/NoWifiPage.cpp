#include "NoWifiPage.h"

NoWifiPage::NoWifiPage(const char* accessPointName)
    : accessPointName_(accessPointName) {}

lv_obj_t* NoWifiPage::create() {
  if (screen_ != nullptr) {
    return screen_;
  }

  screen_ = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x101827), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

  title_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(title_, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title_, lv_color_hex(0xF7C873), 0);
  lv_obj_align(title_, LV_ALIGN_CENTER, 0, -72);

  instructions_ = lv_label_create(screen_);
  lv_obj_set_width(instructions_, 300);
  lv_obj_set_style_text_align(instructions_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(instructions_, lv_color_hex(0xE6EDF7), 0);
  lv_obj_align(instructions_, LV_ALIGN_CENTER, 0, -28);

  accessPointLabel_ = lv_label_create(screen_);
  lv_label_set_text(accessPointLabel_, accessPointName_);
  lv_obj_set_style_text_font(accessPointLabel_, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(accessPointLabel_, lv_color_hex(0x55E6A5), 0);
  lv_obj_align(accessPointLabel_, LV_ALIGN_CENTER, 0, 15);

  details_ = lv_label_create(screen_);
  lv_obj_set_width(details_, 300);
  lv_obj_set_style_text_align(details_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(details_, lv_color_hex(0xE6EDF7), 0);
  lv_obj_align(details_, LV_ALIGN_CENTER, 0, 62);
  showConnecting();

  return screen_;
}

void NoWifiPage::onShow() {}

void NoWifiPage::update() {}

void NoWifiPage::showConnecting() {
  lv_label_set_text(title_, "INITIALIZING WI-FI");
  lv_label_set_text(instructions_,
                    "Verifying saved Wi-Fi and trying to connect...");
  lv_obj_set_hidden(accessPointLabel_, true);
  lv_obj_set_hidden(details_, true);
}

void NoWifiPage::showAccessPointInstructions() {
  lv_label_set_text(title_, "WI-FI SETUP");
  lv_label_set_text(instructions_,
                    "Connect your phone to this Wi-Fi network:");
  lv_obj_set_hidden(accessPointLabel_, false);
  lv_label_set_text(details_,
                    "Then choose your Wi-Fi and enter its password.");
  lv_obj_set_hidden(details_, false);
}
