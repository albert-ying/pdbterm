#include "SixelEncoder.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_set>

std::string SixelEncoder::build_palette() {
    std::string pal;
    pal.reserve(256 * 20);
    for (int i = 0; i < 256; i++) {
        const RGBA& c = Palettes::ID2RGBA[i];
        int r = (int)(c.r * 100 / 255);
        int g = (int)(c.g * 100 / 255);
        int b = (int)(c.b * 100 / 255);
        pal += "#" + std::to_string(i) + ";2;" +
               std::to_string(r) + ";" +
               std::to_string(g) + ";" +
               std::to_string(b);
    }
    return pal;
}

int SixelEncoder::nearest_palette_color(uint8_t r, uint8_t g, uint8_t b) {
    static const int levels[6] = {0, 95, 135, 175, 215, 255};

    auto nearest_level = [&](uint8_t v) -> int {
        int best = 0;
        int best_dist = abs(v - levels[0]);
        for (int i = 1; i < 6; i++) {
            int d = abs(v - levels[i]);
            if (d < best_dist) { best_dist = d; best = i; }
        }
        return best;
    };

    int ri = nearest_level(r);
    int gi = nearest_level(g);
    int bi = nearest_level(b);
    return 16 + ri * 36 + gi * 6 + bi;
}

void SixelEncoder::encode_band(std::string& out,
                                const std::vector<int>& palette_pixels,
                                int width, int height, int band_y) {
    std::unordered_set<int> active_colors;
    for (int row = band_y; row < std::min(band_y + 6, height); row++) {
        for (int x = 0; x < width; x++) {
            int c = palette_pixels[row * width + x];
            if (c >= 0) active_colors.insert(c);
        }
    }

    bool first_color = true;
    for (int color : active_colors) {
        if (!first_color) {
            out += '$';
        }
        first_color = false;

        out += '#';
        out += std::to_string(color);

        int run_char = -1;
        int run_len = 0;

        auto flush_run = [&]() {
            if (run_len <= 0) return;
            if (run_len <= 3) {
                for (int i = 0; i < run_len; i++) out += (char)run_char;
            } else {
                out += '!';
                out += std::to_string(run_len);
                out += (char)run_char;
            }
        };

        for (int x = 0; x < width; x++) {
            int sixel_val = 0;
            for (int bit = 0; bit < 6; bit++) {
                int row = band_y + bit;
                if (row < height) {
                    int c = palette_pixels[row * width + x];
                    if (c == color) {
                        sixel_val |= (1 << bit);
                    }
                }
            }
            int ch = sixel_val + 63;

            if (ch == run_char) {
                run_len++;
            } else {
                flush_run();
                run_char = ch;
                run_len = 1;
            }
        }
        flush_run();
    }

    out += '-';
}

std::string SixelEncoder::encode(const std::vector<RGBA>& pixels,
                                  int width, int height,
                                  uint8_t bg_r, uint8_t bg_g, uint8_t bg_b) {
    std::vector<int> palette_pixels(width * height, -1);
    for (int i = 0; i < width * height; i++) {
        const RGBA& px = pixels[i];
        if (px.a < 16) continue;

        float a = px.a / 255.0f;
        uint8_t r = (uint8_t)(px.r * a + bg_r * (1.0f - a));
        uint8_t g = (uint8_t)(px.g * a + bg_g * (1.0f - a));
        uint8_t b = (uint8_t)(px.b * a + bg_b * (1.0f - a));
        palette_pixels[i] = nearest_palette_color(r, g, b);
    }

    std::string out;
    out.reserve(width * height);

    out += "\033P0;1;q";
    out += build_palette();

    for (int band_y = 0; band_y < height; band_y += 6) {
        encode_band(out, palette_pixels, width, height, band_y);
    }

    out += "\033\\";

    return out;
}
