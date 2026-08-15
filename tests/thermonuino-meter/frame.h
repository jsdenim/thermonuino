#pragma once
#include <Arduino.h>

#pragma pack(push, 1)
struct Frame {
  uint8_t ver;
  uint8_t seq;
  uint8_t dev_id;
  uint8_t zone;
  int16_t temp_c_x100;
  uint16_t hum_x100;
  uint16_t bat_mv;
  uint8_t door_open;
  uint8_t door_learned;
  uint8_t door_count;
  uint8_t motion_count;
  uint8_t boost_req_count;
  uint8_t crc;
};
#pragma pack(pop)