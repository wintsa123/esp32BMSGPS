#include "esp_bms_companion_codec.h"

static bool decode_bytes(esp_bms_companion_decoder_t *d, const uint8_t *data, size_t length, bool write)
{
    for (size_t i = 0; i < length; ++i) {
        uint16_t run;
        if (d->run_state == 1) {
            d->run_length = data[i]; d->run_state = 2; continue;
        } else if (d->run_state == 2) {
            run = d->run_length | ((uint16_t)data[i] << 8);
            d->run_state = 0;
            if (run < 16) return false;
        } else {
            d->run_color = data[i] & 15U;
            if ((data[i] >> 4) == 15U) { d->run_state = 1; continue; }
            run = (data[i] >> 4) + 1U;
        }
        if (run > d->count - d->written) return false;
        const size_t end = d->written + run;
        if (!write) { d->written = end; continue; }
        while (d->written < end) {
            const size_t p = d->written++;
            if (!(p & 1U)) d->pixels[p / 2U] = d->run_color << 4;
            else d->pixels[p / 2U] |= d->run_color;
        }
    }
    d->received += length;
    return true;
}

bool esp_bms_companion_decode(esp_bms_companion_decoder_t *d,
                              size_t offset, const uint8_t *data, size_t length)
{
    if (!d || !d->pixels || !data || offset != d->received ||
        d->count / 2U > d->capacity || (d->count & 1U) || d->written > d->count) return false;
    esp_bms_companion_decoder_t check = *d;
    if (!decode_bytes(&check, data, length, false)) return false;
    return decode_bytes(d, data, length, true);
}
