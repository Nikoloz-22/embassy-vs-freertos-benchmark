#include "crc32.h"
// Separate compilation unit, -O3, no LTO; prevents constant precomputation
// at the caller without adding volatile loads to the algorithm itself.
__attribute__((noinline))
uint32_t bench_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; ++j) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

