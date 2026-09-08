/* Research-only binding to Niels Ferguson's separately acquired Twofish 0.3.
 * No game keys, game code, or third-party implementation are embedded here. */
#include <stdint.h>
#include <stddef.h>
#include "twofish.h"

__declspec(dllexport) void initialize(void) { Twofish_initialise(); }
__declspec(dllexport) void transform(unsigned char *data, size_t size,
    unsigned char *key, const unsigned char *iv) {
    Twofish_key schedule;
    unsigned char counter[16], block[16];
    Twofish_prepare_key(key, 16, &schedule);
    for (int i = 0; i < 16; ++i) counter[i] = iv[i];
    for (size_t offset = 0; offset < size; offset += 16) {
        Twofish_encrypt(&schedule, counter, block);
        for (size_t i = 0; i < 16 && offset + i < size; ++i) data[offset + i] ^= block[i];
        for (int i = 0; i < 16; ++i) if (++counter[i]) break;
    }
}
