#pragma once
#include <cmath>
#include <cstdint>

struct TempHistory {
  static const int SLOTS = 96;          // 24h / 15min
  static const uint32_t SLOT_SEC = 900; // 15 minutes
  static const uint32_t WINDOW = 86400; // 24 hours

  float slot_min[SLOTS];
  float slot_max[SLOTS];
  uint32_t slot_epoch[SLOTS];           // start-of-slot timestamp

  void init() {
    for (int i = 0; i < SLOTS; i++) {
      slot_min[i] = NAN;
      slot_max[i] = NAN;
      slot_epoch[i] = 0;
    }
  }

  void record(float temp, uint32_t now) {
    if (now < SLOT_SEC) return; // clock not synced yet
    uint32_t slot_start = (now / SLOT_SEC) * SLOT_SEC;
    int idx = (int)((slot_start / SLOT_SEC) % SLOTS);

    // If this slot is stale (different epoch), reset it
    if (slot_epoch[idx] != slot_start) {
      slot_min[idx] = temp;
      slot_max[idx] = temp;
      slot_epoch[idx] = slot_start;
    } else {
      if (temp < slot_min[idx]) slot_min[idx] = temp;
      if (temp > slot_max[idx]) slot_max[idx] = temp;
    }
  }

  float get_min(uint32_t now) {
    float result = NAN;
    uint32_t cutoff = (now > WINDOW) ? (now - WINDOW) : 0;
    for (int i = 0; i < SLOTS; i++) {
      if (slot_epoch[i] == 0 || slot_epoch[i] < cutoff) continue;
      if (std::isnan(slot_min[i])) continue;
      if (std::isnan(result) || slot_min[i] < result) result = slot_min[i];
    }
    return result;
  }

  float get_max(uint32_t now) {
    float result = NAN;
    uint32_t cutoff = (now > WINDOW) ? (now - WINDOW) : 0;
    for (int i = 0; i < SLOTS; i++) {
      if (slot_epoch[i] == 0 || slot_epoch[i] < cutoff) continue;
      if (std::isnan(slot_max[i])) continue;
      if (std::isnan(result) || slot_max[i] > result) result = slot_max[i];
    }
    return result;
  }
};

static TempHistory van_temp_hist;
static TempHistory outside_temp_hist;
static TempHistory fridge_temp_hist;
static TempHistory solar_power_hist;
static TempHistory shunt_voltage_hist;
static TempHistory shunt_soc_hist;
static TempHistory shunt_current_hist;
static TempHistory mains_power_hist;
static TempHistory mains_current_hist;
