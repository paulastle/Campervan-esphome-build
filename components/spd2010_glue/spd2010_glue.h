#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "driver/gpio.h"
#include "lvgl.h"

namespace esphome {
namespace spd2010_glue {

class Spd2010LvglGlue : public Component, public i2c::I2CDevice {
 public:
  void begin();
  void set_screen_size(uint16_t w, uint16_t h) { w_ = w; h_ = h; }
  void set_swap_xy(bool b) { swap_xy_ = b; }
  void set_mirror_x(bool b) { mirror_x_ = b; }
  void set_mirror_y(bool b) { mirror_y_ = b; }
  void set_int_gpio(int gpio) { int_gpio_ = gpio; }

  void setup() override;
  void dump_config() override;
  void loop() override;

  float get_setup_priority() const override {
    return esphome::setup_priority::LATE;
  }

 protected:
  // I2C helpers using ESPHome's I2C bus
  bool spd_write_reg16_(uint16_t reg, uint16_t val_le);
  bool spd_read_reg_(uint16_t reg, uint8_t *out, size_t len);

  void register_lvgl_indev_();
  static void lvgl_read_cb_(lv_indev_drv_t *drv, lv_indev_data_t *data);
  void update_irq_stuck_detector_(uint32_t now_ms, int irq_level);

  // Touch status parsing
  struct tp_status_t {
    bool pt_exist;
    bool gesture;
    bool aux;
    bool tic_busy;
    bool tic_in_bios;
    bool tic_in_cpu;
    bool tint_low;
    bool cpu_run;
    uint16_t read_len;
  };

  struct tp_hdp_status_t {
    uint8_t status;
    uint16_t next_packet_len;
  };

  struct touch_point_t {
    uint16_t x;
    uint16_t y;
    uint8_t strength;
  };

  bool read_tp_status_(tp_status_t *st);
  bool read_hdp_status_(tp_hdp_status_t *hs);
  bool drain_hdp_remain_(uint16_t len);
  bool clear_int_();
  bool cpu_start_();
  bool point_mode_();
  bool start_cmd_();
  bool post_read_irq_service_(bool *cleared);
  static uint8_t parse_hdp_points_(const uint8_t *d, size_t len, touch_point_t *out, uint8_t max_points);

  // LVGL indev
  lv_indev_drv_t indev_drv_{};
  lv_indev_t *indev_{nullptr};

  // Screen & orientation
  uint16_t w_{412}, h_{412};
  bool swap_xy_{false}, mirror_x_{false}, mirror_y_{false};
  int int_gpio_{4};

  // State
  static constexpr uint32_t kPollMs = 20;
  static constexpr uint32_t kTouchPollMs = 10;
  static constexpr size_t kMaxHdpBytes = 128;

  uint32_t last_poll_ms_{0};
  uint32_t last_touch_poll_ms_{0};
  bool last_pressed_{false};
  uint16_t last_x_{0}, last_y_{0};

  // IRQ stuck detection
  uint32_t irq_last_change_ms_{0};
  int irq_last_level_{1};
  uint32_t irq_stuck_threshold_ms_{400};

  bool indev_registered_{false};
  bool started_{false};
  uint8_t hdp_buf_[kMaxHdpBytes];
};

}  // namespace spd2010_glue
}  // namespace esphome
