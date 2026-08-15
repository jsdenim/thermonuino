#pragma once
#include <Arduino.h>

struct Statut {
  uint32_t datetime;    // secondes depuis Epoch (Unix time)
  uint8_t  consigne[4]; // 4 zones: [0..3]
};

namespace Proto {
  // Délimiteurs
  static const uint8_t SOF1 = 0xA5;
  static const uint8_t SOF2 = 0x5A;

  // Version + type
  static const uint8_t VER  = 0x02;        // <-- nouvelle version
  static const uint8_t TYPE_STATUT = 0x01;

  // Longueur du payload pour Statut (4 + 4 = 8)
  static const uint8_t PAYLOAD_LEN = 8;

  // ---- CRC8 Dallas/Maxim (poly 0x31, init 0x00) ----
  inline uint8_t crc8_maxim(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
      uint8_t inbyte = data[i];
      for (uint8_t j = 0; j < 8; j++) {
        uint8_t mix = (crc ^ inbyte) & 0x01;
        crc >>= 1;
        if (mix) crc ^= 0x8C; // 0x31 "reversed"
        inbyte >>= 1;
      }
    }
    return crc;
  }

  // ---- Sérialisation (8 octets) ----
  inline void serializeStatut(const Statut& s, uint8_t* out /*>=8*/) {
    // datetime en little-endian
    out[0] = (uint8_t)(s.datetime & 0xFF);
    out[1] = (uint8_t)((s.datetime >> 8) & 0xFF);
    out[2] = (uint8_t)((s.datetime >> 16) & 0xFF);
    out[3] = (uint8_t)((s.datetime >> 24) & 0xFF);
    // consigne[0..3]
    out[4] = s.consigne[0];
    out[5] = s.consigne[1];
    out[6] = s.consigne[2];
    out[7] = s.consigne[3];
  }

  inline void deserializeStatut(const uint8_t* in /*8*/, Statut& s) {
    s.datetime = (uint32_t)in[0]
               | ((uint32_t)in[1] << 8)
               | ((uint32_t)in[2] << 16)
               | ((uint32_t)in[3] << 24);
    s.consigne[0] = in[4];
    s.consigne[1] = in[5];
    s.consigne[2] = in[6];
    s.consigne[3] = in[7];
  }

  // ---- Envoi d'une trame Statut ----
  inline void sendStatut(Stream& s, const Statut& st) {
    uint8_t payload[PAYLOAD_LEN];
    serializeStatut(st, payload);

    uint8_t buf[3 + PAYLOAD_LEN];   // VER, TYPE, LEN, PAYLOAD
    buf[0] = VER;
    buf[1] = TYPE_STATUT;
    buf[2] = PAYLOAD_LEN;
    memcpy(&buf[3], payload, PAYLOAD_LEN);

    uint8_t crc = crc8_maxim(buf, sizeof(buf));

    s.write(SOF1);
    s.write(SOF2);
    s.write(buf, sizeof(buf));
    s.write(crc);
  }

  // ---- Réception (parseur par états) ----
  class Rx {
  public:
    Rx(): state(0), idx(0), expectedLen(0) {}

    // Retourne true si un Statut complet a été reçu (rempli 'out')
    bool poll(Stream& s, Statut& out) {
      while (s.available()) {
        uint8_t b = (uint8_t)s.read();
        switch (state) {
          case 0: // SOF1
            state = (b == SOF1) ? 1 : 0;
            break;
          case 1: // SOF2
            if (b == SOF2) { state = 2; idx = 0; }
            else state = 0;
            break;
          case 2: // VER
            frame[0] = b; state = 3; break;
          case 3: // TYPE
            frame[1] = b; state = 4; break;
          case 4: // LEN
            frame[2] = b;
            expectedLen = b;
            if (expectedLen > sizeof(payload)) { state = 0; break; }
            idx = 0; state = 5; break;
          case 5: // PAYLOAD
            payload[idx++] = b;
            if (idx >= expectedLen) state = 6;
            break;
          case 6: { // CRC
            uint8_t buf[3 + sizeof(payload)];
            memcpy(buf, frame, 3);
            memcpy(buf + 3, payload, expectedLen);
            uint8_t crcCalc = crc8_maxim(buf, 3 + expectedLen);

            bool ok = (b == crcCalc)
                   && (frame[0] == VER)
                   && (frame[1] == TYPE_STATUT)
                   && (expectedLen == PAYLOAD_LEN);

            state = 0;
            if (ok) {
              deserializeStatut(payload, out);
              return true;
            }
            break;
          }
        }
      }
      return false;
    }
  private:
    uint8_t state;
    uint8_t idx;
    uint8_t expectedLen;
    uint8_t frame[3];               // VER, TYPE, LEN
    uint8_t payload[PAYLOAD_LEN];   // pile locale
  };
}
