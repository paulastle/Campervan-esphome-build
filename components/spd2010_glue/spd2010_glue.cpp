#include "spd2010_glue.h"
#include "esphome/core/log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include <algorithm>

namespace esphome {
namespace spd2010_glue {

static const char *const TAG = "spd2010_glue";

// --- Register addresses ---
static constexpr uint16_t REG_TP_STATUS_LEN = 0x2000;
static constexpr uint16_t REG_HDP           = 0x0003;
static constexpr uint16_t REG_HDP_STATUS    = 0xFC02;
static constexpr uint16_t REG_CLEAR_INT     = 0x0200;
static constexpr uint16_t REG_CPU_START     = 0x0400;
static constexpr uint16_t REG_POINT_MODE    = 0x5000;
static constexpr uint16_t REG_START         = 0x4600;

// --- I2C helpers using ESPHome's I2C bus ---

bool Spd2010LvglGlue::spd_write_reg16_(uint16_t reg, uint16_t val_le) {
  uint8_t buf[4] = {
    (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
    (uint8_t)(val_le & 0xFF), (uint8_t)(val_le >> 8)
  };
  return this->write(buf, sizeof(buf)) == i2c::ERROR_OK;
}

bool Spd2010LvglGlue::spd_read_reg_(uint16_t reg, uint8_t *out, size_t len) {
  uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
  if (this->write(addr, sizeof(addr), false) != i2c::ERROR_OK)
    return false;
  return this->read(out, len) == i2c::ERROR_OK;
}

// --- Status/control helpers ---

bool Spd2010LvglGlue::read_tp_status_(tp_status_t *st) {
  uint8_t d[4] = {0};
  if (!spd_read_reg_(REG_TP_STATUS_LEN, d, sizeof(d))) return false;
  st->pt_exist    = (d[0] & 0x01);
  st->gesture     = (d[0] & 0x02);
  st->aux         = (d[0] & 0x08);
  st->tic_busy    = (d[1] & 0x80);
  st->tic_in_bios = (d[1] & 0x40);
  st->tic_in_cpu  = (d[1] & 0x20);
  st->tint_low    = (d[1] & 0x10);
  st->cpu_run     = (d[1] & 0x08);
  st->read_len    = (uint16_t)d[3] << 8 | (uint16_t)d[2];
  return true;
}

bool Spd2010LvglGlue::read_hdp_status_(tp_hdp_status_t *hs) {
  uint8_t d[8] = {0};
  if (!spd_read_reg_(REG_HDP_STATUS, d, sizeof(d))) return false;
  hs->status = d[5];
  hs->next_packet_len = (uint16_t)d[2] | ((uint16_t)d[3] << 8);
  return true;
}

bool Spd2010LvglGlue::drain_hdp_remain_(uint16_t len) {
  if (len == 0) return true;
  uint8_t tmp[128];
  while (len > 0) {
    size_t chunk = std::min<size_t>(len, sizeof(tmp));
    if (!spd_read_reg_(REG_HDP, tmp, chunk)) return false;
    len -= chunk;
  }
  return true;
}

bool Spd2010LvglGlue::clear_int_() {
  if (!spd_write_reg16_(REG_CLEAR_INT, 0x0001)) return false;
  esp_rom_delay_us(200);
  return true;
}

bool Spd2010LvglGlue::cpu_start_() {
  if (!spd_write_reg16_(REG_CPU_START, 0x0001)) return false;
  esp_rom_delay_us(200);
  return true;
}

bool Spd2010LvglGlue::point_mode_() {
  if (!spd_write_reg16_(REG_POINT_MODE, 0x0000)) return false;
  esp_rom_delay_us(200);
  return true;
}

bool Spd2010LvglGlue::start_cmd_() {
  if (!spd_write_reg16_(REG_START, 0x0000)) return false;
  esp_rom_delay_us(200);
  return true;
}

bool Spd2010LvglGlue::post_read_irq_service_(bool *cleared) {
  tp_status_t st{};
  if (!read_tp_status_(&st)) return false;

  if (st.tic_in_bios) {
    clear_int_();
    cpu_start_();
    return true;
  }
  if (st.tic_in_cpu) {
    point_mode_();
    start_cmd_();
    clear_int_();
    return true;
  }
  if (st.cpu_run && st.read_len == 0) {
    clear_int_();
    return true;
  }

  if (st.pt_exist || st.gesture) {
    tp_hdp_status_t hs{};
    for (int i = 0; i < 10; i++) {
      if (!read_hdp_status_(&hs)) break;
      if (hs.status == 0x82) {
        clear_int_();
        if (cleared) *cleared = true;
        break;
      } else if (hs.status == 0x00 && hs.next_packet_len > 0) {
        drain_hdp_remain_(hs.next_packet_len);
      } else {
        break;
      }
    }
    return true;
  }

  if (st.cpu_run && st.aux) {
    clear_int_();
  }
  return true;
}

uint8_t Spd2010LvglGlue::parse_hdp_points_(const uint8_t *d, size_t len,
                                             touch_point_t *out, uint8_t max_points) {
  if (!d || len < 10 || max_points == 0) return 0;

  if (d[4] == 0xF6) return 0;  // gesture-only

  const size_t raw_count = (len - 4) / 6;
  const uint8_t count = static_cast<uint8_t>(std::min(raw_count, static_cast<size_t>(max_points)));

  for (uint8_t i = 0; i < count; i++) {
    const size_t off = i * 6;
    out[i].x        = (static_cast<uint16_t>(d[7 + off] & 0xF0) << 4) | d[5 + off];
    out[i].y        = (static_cast<uint16_t>(d[7 + off] & 0x0F) << 8) | d[6 + off];
    out[i].strength = d[8 + off];
  }
  return count;
}

// --- Component lifecycle ---

void Spd2010LvglGlue::setup() {
  ESP_LOGI(TAG, "Deferring SPD2010 init to on_boot/begin()");
}

void Spd2010LvglGlue::begin() {
  if (started_) { ESP_LOGI(TAG, "Touch already started; skipping."); return; }

  // Verify the SPD2010 responds on the I2C bus
  uint8_t test[4] = {0};
  if (!spd_read_reg_(REG_TP_STATUS_LEN, test, sizeof(test))) {
    ESP_LOGE(TAG, "SPD2010 not responding at 0x53");
    return;
  }
  ESP_LOGI(TAG, "SPD2010 status: %02x %02x len=%u",
           test[0], test[1], (uint16_t)test[3] << 8 | test[2]);

  // Handle controller boot states
  for (int attempt = 0; attempt < 5; attempt++) {
    tp_status_t st{};
    if (!read_tp_status_(&st)) break;
    ESP_LOGI(TAG, "Boot state[%d]: bios=%d cpu=%d run=%d pt=%d len=%u",
             attempt, st.tic_in_bios, st.tic_in_cpu, st.cpu_run, st.pt_exist, st.read_len);
    if (st.tic_in_bios) {
      clear_int_(); cpu_start_();
      vTaskDelay(pdMS_TO_TICKS(100));
    } else if (st.tic_in_cpu) {
      point_mode_(); start_cmd_(); clear_int_();
      vTaskDelay(pdMS_TO_TICKS(100));
    } else if (st.cpu_run) {
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  // Enable point mode and start
  bool pm = point_mode_();
  bool sc = start_cmd_();
  bool ci = clear_int_();
  ESP_LOGI(TAG, "Init commands: point_mode=%d start=%d clear_int=%d", pm, sc, ci);

  // Configure INT pin as input
  gpio_config_t io_gpio{};
  io_gpio.pin_bit_mask = 1ULL << int_gpio_;
  io_gpio.mode = GPIO_MODE_INPUT;
  io_gpio.pull_up_en = GPIO_PULLUP_ENABLE;
  io_gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_gpio.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io_gpio);

  started_ = true;
  ESP_LOGI(TAG, "Touch initialized, INT=%d",
           gpio_get_level(static_cast<gpio_num_t>(int_gpio_)));
}

void Spd2010LvglGlue::dump_config() {
  ESP_LOGCONFIG(TAG, "SPD2010 LVGL glue (I2C addr=0x53)");
  ESP_LOGCONFIG(TAG, "  Screen: %ux%u, swap_xy=%s, mirror_x=%s, mirror_y=%s, INT=GPIO%d",
                w_, h_,
                swap_xy_ ? "true" : "false",
                mirror_x_ ? "true" : "false",
                mirror_y_ ? "true" : "false",
                int_gpio_);
}

void Spd2010LvglGlue::register_lvgl_indev_() {
  lv_indev_drv_init(&this->indev_drv_);
  this->indev_drv_.type      = LV_INDEV_TYPE_POINTER;
  this->indev_drv_.read_cb   = &Spd2010LvglGlue::lvgl_read_cb_;
  this->indev_drv_.user_data = this;
  this->indev_drv_.disp      = lv_disp_get_default();
  this->indev_ = lv_indev_drv_register(&this->indev_drv_);
  ESP_LOGI(TAG, "LVGL indev registered");
}

void Spd2010LvglGlue::loop() {
  const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

  if (!started_) return;

  if (!indev_registered_ && lv_disp_get_default()) {
    register_lvgl_indev_();
    indev_registered_ = true;
  }

  if (now_ms - last_touch_poll_ms_ < kTouchPollMs) return;
  last_touch_poll_ms_ = now_ms;

  // Poll status register directly (don't rely on IRQ pin)
  tp_status_t st{};
  if (!read_tp_status_(&st)) return;

  // Handle controller boot states in loop
  if (st.tic_in_bios) { clear_int_(); cpu_start_(); return; }
  if (st.tic_in_cpu) { point_mode_(); start_cmd_(); clear_int_(); return; }

  if (!(st.pt_exist || st.gesture) || st.read_len == 0) {
    if (st.cpu_run && st.read_len == 0) {
      int irq = gpio_get_level(static_cast<gpio_num_t>(int_gpio_));
      if (irq == 0) clear_int_();
    }
    if (last_pressed_) last_pressed_ = false;
    return;
  }

  const size_t to_read = std::min<size_t>(st.read_len, kMaxHdpBytes);
  if (!spd_read_reg_(REG_HDP, hdp_buf_, to_read)) {
    ESP_LOGW(TAG, "HDP read failed");
    return;
  }

  touch_point_t points[5] = {};
  uint8_t cnt = parse_hdp_points_(hdp_buf_, to_read, points, 5);

  if (cnt > 0) {
    last_pressed_ = true;
    uint16_t x = points[0].x;
    uint16_t y = points[0].y;

    if (swap_xy_) { uint16_t tmp = x; x = y; y = tmp; }
    if (mirror_x_ && w_ > 0) x = w_ - 1 - x;
    if (mirror_y_ && h_ > 0) y = h_ - 1 - y;

    last_x_ = x;
    last_y_ = y;
  } else {
    last_pressed_ = false;
  }

  bool cleared = false;
  post_read_irq_service_(&cleared);
  update_irq_stuck_detector_(now_ms, gpio_get_level(static_cast<gpio_num_t>(int_gpio_)));
}

void Spd2010LvglGlue::lvgl_read_cb_(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  auto *self = reinterpret_cast<Spd2010LvglGlue *>(drv->user_data);
  if (!self || !self->started_) {
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = data->point.y = 0;
    return;
  }

  data->state   = self->last_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = self->last_x_;
  data->point.y = self->last_y_;
}

void Spd2010LvglGlue::update_irq_stuck_detector_(uint32_t now_ms, int irq_level) {
  if (irq_level != irq_last_level_) {
    irq_last_level_ = irq_level;
    irq_last_change_ms_ = now_ms;
    return;
  }

  if (irq_level == 0) {
    uint32_t stuck_ms = now_ms - irq_last_change_ms_;
    if (stuck_ms > irq_stuck_threshold_ms_) {
      ESP_LOGW(TAG, "IRQ stuck low for %u ms; force-clearing", (unsigned)stuck_ms);
      clear_int_();
      vTaskDelay(pdMS_TO_TICKS(1));
      int irq_after = gpio_get_level((gpio_num_t)int_gpio_);
      if (irq_after != irq_level) {
        irq_last_level_ = irq_after;
        irq_last_change_ms_ = now_ms;
      }
    }
  }
}

}  // namespace spd2010_glue
}  // namespace esphome
