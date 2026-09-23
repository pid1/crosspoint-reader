#pragma once

// RFC 1321 MD5 behind the ESP32 core's MD5Builder surface, so the host build
// hashes the same bytes the device does. Streaming and lossless: a digest here
// is only worth anything if nothing is dropped between add() and calculate().

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

class MD5Builder {
 public:
  void begin() {
    state[0] = 0x67452301u;
    state[1] = 0xefcdab89u;
    state[2] = 0x98badcfeu;
    state[3] = 0x10325476u;
    length = 0;
    pending = 0;
  }

  void add(const uint8_t* data, size_t size) {
    length += size;
    while (size > 0) {
      const size_t take = std::min(size, sizeof(block) - pending);
      std::memcpy(block + pending, data, take);
      pending += take;
      data += take;
      size -= take;
      if (pending == sizeof(block)) {
        transform(block);
        pending = 0;
      }
    }
  }

  void add(const char* text) { add(reinterpret_cast<const uint8_t*>(text), std::strlen(text)); }

  void calculate() {
    const uint64_t bits = length * 8;
    static const uint8_t padding[64] = {0x80};
    add(padding, pending < 56 ? 56 - pending : 120 - pending);
    uint8_t tail[8];
    for (int i = 0; i < 8; i++) tail[i] = static_cast<uint8_t>(bits >> (8 * i));
    add(tail, sizeof(tail));

    char out[33];
    for (int i = 0; i < 16; i++) {
      const uint8_t byte = static_cast<uint8_t>(state[i / 4] >> (8 * (i % 4)));
      std::snprintf(out + i * 2, 3, "%02x", byte);
    }
    hex.assign(out, 32);
  }

  const std::string& toString() const { return hex; }

 private:
  static uint32_t rotate(const uint32_t value, const int by) { return (value << by) | (value >> (32 - by)); }

  void transform(const uint8_t* chunk) {
    static const uint32_t K[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};
    static const int S[64] = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
                              14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                              4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};

    uint32_t words[16];
    for (int i = 0; i < 16; i++) {
      words[i] = static_cast<uint32_t>(chunk[i * 4]) | static_cast<uint32_t>(chunk[i * 4 + 1]) << 8 |
                 static_cast<uint32_t>(chunk[i * 4 + 2]) << 16 | static_cast<uint32_t>(chunk[i * 4 + 3]) << 24;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];

    for (int i = 0; i < 64; i++) {
      uint32_t f;
      int g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      const uint32_t temp = d;
      d = c;
      c = b;
      b = b + rotate(a + f + K[i] + words[g], S[i]);
      a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
  }

  uint32_t state[4] = {};
  uint8_t block[64] = {};
  size_t pending = 0;
  uint64_t length = 0;
  std::string hex;
};
