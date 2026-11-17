#include "esp_bms_companion_codec.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t pixels[80] = {0};
    esp_bms_companion_decoder_t d = {.pixels=pixels,.capacity=80,.count=160};
    const uint8_t long_run[] = {0xf8, 160, 0};
    assert(esp_bms_companion_decode(&d,0,long_run,1));
    assert(d.written == 0 && d.run_state == 1);
    assert(esp_bms_companion_decode(&d,1,long_run+1,1));
    assert(esp_bms_companion_decode(&d,2,long_run+2,1));
    assert(d.written == 160 && d.run_state == 0);
    for (unsigned i=0;i<80;i++) assert(pixels[i] == 0x88);
    assert(!esp_bms_companion_decode(&d,3,long_run,3));
    assert(d.written == 160 && d.received == 3);
    d=(esp_bms_companion_decoder_t){.pixels=pixels,.capacity=80,.count=4};
    const uint8_t short_run[] = {0x01,0x22};
    assert(!esp_bms_companion_decode(&d,1,short_run,2));
    assert(esp_bms_companion_decode(&d,0,short_run,2));
    assert(pixels[0] == 0x12 && pixels[1] == 0x22);
    d=(esp_bms_companion_decoder_t){.pixels=pixels,.capacity=80,.count=4};
    const uint8_t invalid[] = {0x03,0xf1,0xff,0xff};
    const uint8_t saved=pixels[0];
    assert(!esp_bms_companion_decode(&d,0,invalid,4));
    assert(d.received == 0 && d.written == 0 && pixels[0] == saved);
    const uint8_t illegal[] = {0xf1,15,0};
    assert(!esp_bms_companion_decode(&d,0,illegal,3));
    d.count=5;
    assert(!esp_bms_companion_decode(&d,0,short_run,2));
    return 0;
}
