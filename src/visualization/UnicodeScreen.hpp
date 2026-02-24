#pragma once
#include "Protein.hpp"
#include "Atom.hpp"
#include "RenderPoint.hpp"
#include "Palette.hpp"
#include "SixelEncoder.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <map>
#include <cstdint>

struct RGB {
    uint8_t r, g, b;
};

struct Pixel {
    uint8_t r, g, b;
    float depth;
    bool active;
};

enum class ViewMode {
    BACKBONE,
    GRID,
    SURFACE,
};

enum class ColorScheme {
    RAINBOW,     // gradient along full sequence
    CHAIN,       // each chain gets a distinct color
    STRUCTURE,   // by secondary structure (helix/sheet/coil)
};

enum class PaletteType {
    NEON,
    COOL,
    WARM,
    EARTH,
    PASTEL,
    PALETTE_COUNT,  // sentinel for cycling
};

class UnicodeScreen {
public:
    UnicodeScreen(const bool& show_structure, const std::string& mode, bool sixel = false);
    ~UnicodeScreen();

    void set_protein(const std::string& in_file, int ii, const bool& show_structure);
    void normalize_proteins(const std::string& utmatrix);
    void set_tmatrix();
    void set_utmatrix(const std::string& utmatrix, bool onlyU);
    void set_chainfile(const std::string& chainfile, int filesize);

    void set_random_mode(bool enabled);
    bool load_random_pdb();
    bool load_specific_pdb(const std::string& pdb_id);

    void draw_screen();
    bool handle_input();

    void enter_raw_mode();
    void exit_raw_mode();

    bool write_framebuffer_png(const std::string& path);

private:
    int term_cols = 80;
    int term_rows = 24;
    int info_rows = 3;
    int buf_width;
    int buf_height;

    void query_terminal_size();

    std::vector<Pixel> framebuffer;

    // Colors and palettes
    std::vector<RGB> palette_colors;
    RGB bg_color;
    RGB fg_color;
    PaletteType palette_type = PaletteType::NEON;
    ColorScheme color_scheme = ColorScheme::RAINBOW;
    void load_colors();
    void apply_palette();
    RGB interpolate_color(float t);

    // View mode
    ViewMode view_mode = ViewMode::BACKBONE;

    // Data
    std::vector<Protein*> data;
    std::vector<float> pan_x;
    std::vector<float> pan_y;
    std::vector<std::string> chainVec;
    float** vectorpointer = nullptr;
    bool yesUT = false;

    BoundingBox global_bb;
    std::string screen_mode;
    bool screen_show_structure;
    int structNum = -1;
    float zoom_level = 2.8f;
    float focal_offset = 5.0f;

    // Auto-rotation
    bool auto_rotate = true;
    float rotation_speed = 0.02f;

    void auto_rotate_step();
    void project_backbone();
    void project_grid();
    void project_surface();
    void clear_framebuffer();

    void draw_line(int x0, int y0, float z0,
                   int x1, int y1, float z1,
                   RGB color, float brightness);

    void draw_filled_circle(int cx, int cy, float z, int radius,
                            RGB color, float brightness);

    void plot_pixel(int x, int y, float z, RGB color, float brightness);

    RGB depth_shade(RGB color, float brightness);
    RGB get_color_for_point(int point_idx, int total_points);
    RGB get_chain_color(int chain_idx, int total_chains);
    RGB get_ss_color(char ss_type);

    void render_braille();
    void render_sixel();
    void draw_info_overlay();
    void draw_sidebar();

    const char* view_mode_name();
    const char* color_scheme_name();
    const char* palette_name();
    void auto_detect_color_scheme();

    bool use_sixel = false;
    bool random_mode = false;
    int pixel_width = 0;
    int pixel_height = 0;
    bool raw_mode_active = false;

    // Random PDB
    static const std::vector<std::string> notable_pdbs;
    std::string download_pdb(const std::string& pdb_id);
    void reload_protein(const std::string& filepath);

    // Sidebar info (fetched from RCSB API, cached to ~/.cache/pdbterm/)
    std::vector<std::string> sidebar_info;
    void fetch_pdb_info(const std::string& pdb_id);
    void pre_cache_pdb_info();
};
