#pragma once
#include "Palette.hpp"
#include <vector>
#include <string>
#include <cstdint>

class SixelEncoder {
public:
    static std::string encode(const std::vector<RGBA>& pixels,
                              int width, int height,
                              uint8_t bg_r = 0, uint8_t bg_g = 0, uint8_t bg_b = 0);

private:
    static std::string build_palette();
    static int nearest_palette_color(uint8_t r, uint8_t g, uint8_t b);
    static void encode_band(std::string& out,
                            const std::vector<int>& palette_pixels,
                            int width, int height, int band_y);
};
