#include "UnicodeScreen.hpp"
#include "lodepng.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <signal.h>
#include <sys/wait.h>

static struct termios orig_termios;
static const float FOV = 90.0f;
static const float PI = 3.14159265359f;

// --- Terminal management ---

void UnicodeScreen::enter_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    write(STDOUT_FILENO, "\033[?25l", 6);
    write(STDOUT_FILENO, "\033[?1049h", 8);
    std::string set_bg = "\033[48;2;" + std::to_string(bg_color.r) + ";" +
                         std::to_string(bg_color.g) + ";" +
                         std::to_string(bg_color.b) + "m";
    write(STDOUT_FILENO, set_bg.c_str(), set_bg.size());
    write(STDOUT_FILENO, "\033[2J", 4);
    raw_mode_active = true;
}

void UnicodeScreen::exit_raw_mode() {
    if (!raw_mode_active) return;
    write(STDOUT_FILENO, "\033[?25h", 6);
    write(STDOUT_FILENO, "\033[?1049l", 8);
    write(STDOUT_FILENO, "\033[0m", 4);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_active = false;
}

void UnicodeScreen::query_terminal_size() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        term_cols = ws.ws_col;
        term_rows = ws.ws_row;
        if (use_sixel) {
            pixel_width = ws.ws_xpixel;
            pixel_height = ws.ws_ypixel;
        }
    }
    info_rows = 1 + (int)data.size();

    if (use_sixel) {
        // Pixel-level resolution via Sixel
        if (pixel_width > 0 && pixel_height > 0) {
            int cell_px_h = pixel_height / term_rows;
            int render_pixel_h = pixel_height - info_rows * cell_px_h;
            buf_width = pixel_width;
            buf_height = std::max(6, render_pixel_h);
        } else {
            // Fallback: estimate pixel dimensions
            buf_width = term_cols * 8;
            buf_height = (term_rows - info_rows) * 16;
        }
    } else {
        // Braille: 2 dots per col, 4 dots per row
        int render_rows = std::max(4, term_rows - info_rows);
        buf_width = term_cols * 2;
        buf_height = render_rows * 4;
    }
}

// --- Colors ---

static RGB boost_color(RGB c, RGB bg) {
    // Ensure color is visible against bg by guaranteeing minimum contrast
    float c_lum = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
    float bg_lum = 0.299f * bg.r + 0.587f * bg.g + 0.114f * bg.b;
    float diff = std::abs(c_lum - bg_lum);

    if (diff < 80.0f) {
        float scale = (bg_lum < 128.0f) ? 1.6f : 0.6f;
        return {
            (uint8_t)std::clamp((int)(c.r * scale), 0, 255),
            (uint8_t)std::clamp((int)(c.g * scale), 0, 255),
            (uint8_t)std::clamp((int)(c.b * scale), 0, 255),
        };
    }
    return c;
}

void UnicodeScreen::load_colors() {
    bg_color = {18, 18, 24};
    fg_color = {180, 180, 180};

    // Try to load pywal colors
    const char* home = getenv("HOME");
    if (home) {
        std::string colors_path = std::string(home) + "/.cache/wal/colors";
        std::ifstream file(colors_path);
        if (file.is_open()) {
            std::vector<RGB> wal_colors;
            std::string line;
            while (std::getline(file, line)) {
                if (line.empty() || line[0] != '#' || line.size() < 7) continue;
                unsigned int r, g, b;
                if (sscanf(line.c_str(), "#%02x%02x%02x", &r, &g, &b) == 3)
                    wal_colors.push_back({(uint8_t)r, (uint8_t)g, (uint8_t)b});
            }
            if (wal_colors.size() >= 16) {
                bg_color = wal_colors[0];
                fg_color = wal_colors[7];
                pywal_colors.clear();
                pywal_colors.push_back(boost_color(wal_colors[8], bg_color));
                for (int i = 1; i <= 6; i++)
                    pywal_colors.push_back(boost_color(wal_colors[i], bg_color));
            } else if (wal_colors.size() >= 8) {
                bg_color = wal_colors[0];
                fg_color = wal_colors[7];
                pywal_colors.clear();
                for (int i = 1; i <= 6; i++)
                    pywal_colors.push_back(boost_color(wal_colors[i], bg_color));
            }
        }
    }
    // Default to neon if no pywal
    if (pywal_colors.empty()) palette_type = PaletteType::NEON;
    apply_palette();
}

void UnicodeScreen::apply_palette() {
    switch (palette_type) {
        case PaletteType::PYWAL:
            palette_colors = pywal_colors.empty()
                ? std::vector<RGB>{{255,70,70},{255,140,50},{255,210,60},{80,220,80},{50,200,220},{80,120,255},{160,80,255}}
                : pywal_colors;
            break;
        case PaletteType::NEON:
            palette_colors = {{255,70,70},{255,140,50},{255,210,60},{80,220,80},{50,200,220},{80,120,255},{160,80,255},{255,80,180}};
            break;
        case PaletteType::COOL:
            palette_colors = {{60,180,255},{80,120,255},{140,80,255},{180,60,220},{80,200,200},{60,220,160},{100,160,255}};
            break;
        case PaletteType::WARM:
            palette_colors = {{255,60,60},{255,120,40},{255,180,30},{255,220,80},{220,100,60},{255,80,120},{240,160,50}};
            break;
        case PaletteType::EARTH:
            palette_colors = {{180,120,60},{140,160,80},{100,140,120},{160,100,80},{120,150,100},{200,160,100},{160,130,90}};
            break;
        case PaletteType::PASTEL:
            palette_colors = {{255,150,150},{255,200,150},{255,255,150},{150,255,180},{150,220,255},{180,150,255},{255,150,220}};
            break;
        default:
            break;
    }
    // Boost all colors for visibility against bg
    for (auto& c : palette_colors)
        c = boost_color(c, bg_color);
}

RGB UnicodeScreen::interpolate_color(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    int n = (int)palette_colors.size();
    if (n == 0) return {255, 255, 255};
    if (n == 1) return palette_colors[0];

    float idx = t * (n - 1);
    int i = (int)idx;
    float f = idx - i;
    if (i >= n - 1) return palette_colors[n - 1];

    const RGB& a = palette_colors[i];
    const RGB& b = palette_colors[i + 1];
    return {
        (uint8_t)(a.r + (b.r - a.r) * f),
        (uint8_t)(a.g + (b.g - a.g) * f),
        (uint8_t)(a.b + (b.b - a.b) * f),
    };
}

// --- Notable PDB structures ---

const std::vector<std::string> UnicodeScreen::notable_pdbs = {
    "1UBQ", "1CRN", "1MBN", "2HHB", "4HHB", "1HHO", "3HHB",
    "1IGT", "1IGY", "1HZH", "1BRS", "1GFL", "2B3P", "1EMA",
    "2PTC", "3PTB", "1TRN", "4INS", "1ZNI", "1APO",
    "1LYZ", "2LYZ", "1HEL", "1AKE", "4AKE", "1GZM",
    "1HSG", "3HVP", "1AID", "1HIV", "2RH1", "3SN6",
    "1ATP", "1PKN", "1CDK", "2SRC", "1QMZ",
    "1BNA", "1D66", "1ZAA", "3DNA",
    "4V6X", "1JJ2", "1FFK", "4UG0",
    "1AON", "1GRL", "1OEL", "3J3Q",
    "1TIM", "8TIM", "1YPI", "1TPH",
    "1FAS", "1COX", "1PRC", "3OGO",
    "2DHB", "1THB", "1A3N", "1BZ0",
    "1CA2", "2CA2", "1CAH",
    "3CLN", "1CLL", "1CDL",
    "1EFN", "1EMD",
    "7BV2", "6VXX", "6LZG", "6M0J",
    "1AO6", "1MBO", "5MBN",
    "1CCR", "1HRC", "1YCC",
    "1PPT", "1RTP", "1VII",
    "1L2Y", "2JOF", "1LE1",
    "1CPN", "7RSA", "1RNH",
    "1OVA", "1UBI", "1A1M",
};

// --- PDB info cache + RCSB API ---

static std::string json_extract_string(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

static std::string json_extract_number(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '[')) pos++;
    size_t start = pos;
    while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '.')) pos++;
    if (pos == start) return "";
    return json.substr(start, pos - start);
}

static std::string title_case(const std::string& s) {
    std::string r = s;
    bool cap = true;
    for (size_t i = 0; i < r.size(); i++) {
        if (cap && std::isalpha((unsigned char)r[i])) {
            r[i] = std::toupper((unsigned char)r[i]);
            cap = false;
        } else if (!cap && std::isalpha((unsigned char)r[i])) {
            r[i] = std::tolower((unsigned char)r[i]);
        }
        if (r[i] == ' ' || r[i] == '-' || r[i] == ':' || r[i] == '.') cap = true;
    }
    return r;
}

static std::string pdb_cache_dir() {
    const char* home = getenv("HOME");
    return home ? (std::string(home) + "/.cache/pdbterm/pdb_info") : "/tmp/pdbterm_cache";
}

static bool load_pdb_cache(const std::string& pdb_id, std::vector<std::string>& info) {
    std::ifstream in(pdb_cache_dir() + "/" + pdb_id + ".txt");
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) info.push_back(line);
    return !info.empty();
}

static void save_pdb_cache(const std::string& pdb_id, const std::vector<std::string>& info) {
    std::string dir = pdb_cache_dir();
    system(("mkdir -p '" + dir + "'").c_str());
    std::ofstream out(dir + "/" + pdb_id + ".txt");
    for (const auto& l : info) out << l << "\n";
}

// Fetch PDB info from RCSB GraphQL API (standalone, no member state)
static std::vector<std::string> fetch_pdb_info_from_api(const std::string& pdb_id) {
    std::vector<std::string> info;

    std::string query = "{entry(entry_id:\\\"" + pdb_id +
        "\\\"){struct_keywords{pdbx_keywords}"
        "rcsb_entry_info{experimental_method resolution_combined molecular_weight nonpolymer_bound_components}"
        "rcsb_accession_info{deposit_date}"
        "polymer_entities{rcsb_polymer_entity{pdbx_description}entity_src_gen{pdbx_gene_src_scientific_name}}"
        "}}";
    std::string cmd = "curl -s -m 5 https://data.rcsb.org/graphql -H 'Content-Type: application/json' -d '{\"query\":\"" + query + "\"}' 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return info;

    std::string response;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) response += buf;
    pclose(pipe);

    if (response.empty()) return info;

    // Parse fields
    std::string keywords    = json_extract_string(response, "pdbx_keywords");
    std::string method      = json_extract_string(response, "experimental_method");
    std::string resolution  = json_extract_number(response, "resolution_combined");
    std::string deposit     = json_extract_string(response, "deposit_date");
    std::string description = json_extract_string(response, "pdbx_description");
    std::string organism    = json_extract_string(response, "pdbx_gene_src_scientific_name");
    std::string mol_weight  = json_extract_number(response, "molecular_weight");

    // Extract ligands array
    std::string ligands;
    size_t nbc_pos = response.find("\"nonpolymer_bound_components\"");
    if (nbc_pos != std::string::npos) {
        size_t colon = response.find(':', nbc_pos + 29);
        if (colon != std::string::npos) {
            size_t val_start = colon + 1;
            while (val_start < response.size() && response[val_start] == ' ') val_start++;
            if (val_start < response.size() && response[val_start] == '[') {
                size_t arr_end = response.find(']', val_start);
                if (arr_end != std::string::npos) {
                    std::string arr = response.substr(val_start + 1, arr_end - val_start - 1);
                    size_t p = 0;
                    bool first = true;
                    while (p < arr.size()) {
                        size_t q1 = arr.find('"', p);
                        if (q1 == std::string::npos) break;
                        size_t q2 = arr.find('"', q1 + 1);
                        if (q2 == std::string::npos) break;
                        if (!first) ligands += ", ";
                        ligands += arr.substr(q1 + 1, q2 - q1 - 1);
                        first = false;
                        p = q2 + 1;
                    }
                }
            }
        }
    }

    // Build sidebar info
    if (!description.empty())
        info.push_back(title_case(description));

    if (!keywords.empty()) {
        info.push_back(title_case(keywords));
    }

    info.push_back("");  // blank separator

    if (!method.empty()) {
        std::string line = title_case(method);
        if (!resolution.empty()) line += ", " + resolution + " A";
        info.push_back(line);
    }

    if (!mol_weight.empty()) {
        float mw = std::stof(mol_weight);
        char mw_buf[32];
        snprintf(mw_buf, sizeof(mw_buf), "%.1f kDa", mw);
        info.push_back(std::string(mw_buf));
    }

    if (!organism.empty())
        info.push_back(title_case(organism));

    if (!deposit.empty()) {
        std::string d = deposit;
        size_t t_pos = d.find('T');
        if (t_pos != std::string::npos) d = d.substr(0, t_pos);
        info.push_back("Deposited " + d);
    }

    if (!ligands.empty()) {
        info.push_back("");
        info.push_back("Ligands: " + ligands);
    }

    return info;
}

void UnicodeScreen::fetch_pdb_info(const std::string& pdb_id) {
    sidebar_info.clear();
    if (pdb_id.empty()) return;

    if (load_pdb_cache(pdb_id, sidebar_info)) return;

    sidebar_info = fetch_pdb_info_from_api(pdb_id);

    if (!sidebar_info.empty())
        save_pdb_cache(pdb_id, sidebar_info);
}

void UnicodeScreen::pre_cache_pdb_info() {
    signal(SIGCHLD, SIG_IGN);

    pid_t pid = fork();
    if (pid != 0) return;  // parent continues immediately

    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);

    std::vector<std::string> shuffled = notable_pdbs;
    srand((unsigned)time(nullptr) + getpid());
    for (int i = (int)shuffled.size() - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        std::swap(shuffled[i], shuffled[j]);
    }

    int fetched = 0;
    for (const auto& id : shuffled) {
        if (fetched >= 10) break;
        std::vector<std::string> dummy;
        if (load_pdb_cache(id, dummy)) continue;

        auto info = fetch_pdb_info_from_api(id);
        if (!info.empty()) {
            save_pdb_cache(id, info);
            fetched++;
        }
    }
    _exit(0);
}

// --- Color scheme helpers ---

RGB UnicodeScreen::get_chain_color(int chain_idx, int total_chains) {
    if (total_chains <= 1) return palette_colors.empty() ? RGB{255,255,255} : palette_colors[0];
    float t = (float)chain_idx / (total_chains - 1);
    return interpolate_color(t);
}

RGB UnicodeScreen::get_ss_color(char ss_type) {
    switch (ss_type) {
        case 'H': return {220, 60, 60};   // helix: red
        case 'S': return {60, 120, 220};   // sheet: blue
        default:  return {160, 160, 160};  // coil: gray
    }
}

void UnicodeScreen::auto_detect_color_scheme() {
    int total_chains = 0;
    for (auto* p : data)
        total_chains += (int)p->get_chain_length().size();
    color_scheme = (total_chains > 1) ? ColorScheme::CHAIN : ColorScheme::RAINBOW;
}

const char* UnicodeScreen::color_scheme_name() {
    switch (color_scheme) {
        case ColorScheme::RAINBOW:   return "rainbow";
        case ColorScheme::CHAIN:     return "chain";
        case ColorScheme::STRUCTURE: return "structure";
    }
    return "?";
}

const char* UnicodeScreen::palette_name() {
    switch (palette_type) {
        case PaletteType::PYWAL:  return "pywal";
        case PaletteType::NEON:   return "neon";
        case PaletteType::COOL:   return "cool";
        case PaletteType::WARM:   return "warm";
        case PaletteType::EARTH:  return "earth";
        case PaletteType::PASTEL: return "pastel";
        default: return "?";
    }
}

std::string UnicodeScreen::download_pdb(const std::string& pdb_id) {
    std::string tmp_path = "/tmp/pdbterm_random_" + pdb_id + ".cif";

    // Check if already cached
    if (access(tmp_path.c_str(), F_OK) == 0) return tmp_path;

    std::string url = "https://files.rcsb.org/download/" + pdb_id + ".cif";
    std::string cmd = "curl -sL -o " + tmp_path + " " + url + " 2>/dev/null";
    int ret = system(cmd.c_str());
    if (ret != 0) return "";

    // Verify file is not empty / error page
    std::ifstream f(tmp_path, std::ios::ate);
    if (!f.is_open() || f.tellg() < 100) {
        std::remove(tmp_path.c_str());
        return "";
    }
    return tmp_path;
}

void UnicodeScreen::reload_protein(const std::string& filepath) {
    // Clean up old data
    if (vectorpointer) {
        for (size_t i = 0; i < data.size(); i++) delete[] vectorpointer[i];
        delete[] vectorpointer;
        vectorpointer = nullptr;
    }
    for (Protein* p : data) delete p;
    data.clear();
    pan_x.clear();
    pan_y.clear();
    chainVec.clear();

    // Load new protein
    chainVec.push_back("-");
    set_protein(filepath, 0, screen_show_structure);
    set_tmatrix();
    normalize_proteins("");
}

void UnicodeScreen::set_random_mode(bool enabled) {
    random_mode = enabled;
}

bool UnicodeScreen::load_random_pdb() {
    static bool seeded = false;
    if (!seeded) { srand((unsigned)time(nullptr)); seeded = true; }

    for (int attempt = 0; attempt < 5; attempt++) {
        int idx = rand() % notable_pdbs.size();
        const std::string& pdb_id = notable_pdbs[idx];

        std::string filepath = download_pdb(pdb_id);
        if (filepath.empty()) continue;

        try {
            reload_protein(filepath);
            if (!data.empty()) {
                std::string pid = data[0]->get_pdb_id();
                if (!pid.empty()) fetch_pdb_info(pid);
                auto_detect_color_scheme();
            }
            pre_cache_pdb_info();
            return true;
        } catch (...) {
            continue;
        }
    }
    return false;
}

bool UnicodeScreen::load_specific_pdb(const std::string& pdb_id) {
    std::string filepath = download_pdb(pdb_id);
    if (filepath.empty()) return false;

    try {
        reload_protein(filepath);
        if (!data.empty()) {
            fetch_pdb_info(pdb_id);
            auto_detect_color_scheme();
        }
        return true;
    } catch (...) {
        return false;
    }
}

// --- PNG screenshot ---

bool UnicodeScreen::write_framebuffer_png(const std::string& path) {
    // Force 1280x720 for headless rendering
    int saved_bw = buf_width, saved_bh = buf_height;
    buf_width = 1280;
    buf_height = 720;
    framebuffer.resize(buf_width * buf_height);

    clear_framebuffer();

    switch (view_mode) {
        case ViewMode::BACKBONE: project_backbone(); break;
        case ViewMode::GRID:     project_grid();     break;
        case ViewMode::SURFACE:  project_surface();  break;
    }

    // Convert framebuffer to RGBA
    std::vector<unsigned char> image(buf_width * buf_height * 4);
    for (int i = 0; i < buf_width * buf_height; i++) {
        if (framebuffer[i].active) {
            image[i * 4 + 0] = framebuffer[i].r;
            image[i * 4 + 1] = framebuffer[i].g;
            image[i * 4 + 2] = framebuffer[i].b;
            image[i * 4 + 3] = 255;
        } else {
            image[i * 4 + 0] = bg_color.r;
            image[i * 4 + 1] = bg_color.g;
            image[i * 4 + 2] = bg_color.b;
            image[i * 4 + 3] = 255;
        }
    }

    unsigned error = lodepng_encode32_file(path.c_str(), image.data(), buf_width, buf_height);

    // Restore
    buf_width = saved_bw;
    buf_height = saved_bh;
    framebuffer.resize(buf_width * buf_height);

    return error == 0;
}

// --- Constructor / Destructor ---

UnicodeScreen::UnicodeScreen(const bool& show_structure, const std::string& mode, bool sixel) {
    screen_show_structure = show_structure;
    screen_mode = mode;
    use_sixel = sixel;
    load_colors();
}

UnicodeScreen::~UnicodeScreen() {
    exit_raw_mode();
    if (vectorpointer) {
        for (size_t i = 0; i < data.size(); i++) delete[] vectorpointer[i];
        delete[] vectorpointer;
        vectorpointer = nullptr;
    }
    for (Protein* p : data) delete p;
    data.clear();
}

// --- Data setup ---

void UnicodeScreen::set_protein(const std::string& in_file, int ii, const bool& show_structure) {
    Protein* protein = new Protein(in_file, chainVec.at(ii), show_structure);
    data.push_back(protein);
    pan_x.push_back(0.0f);
    pan_y.push_back(0.0f);
}

void UnicodeScreen::set_tmatrix() {
    size_t filenum = data.size();
    vectorpointer = new float*[filenum];
    for (size_t i = 0; i < filenum; i++)
        vectorpointer[i] = new float[3]{0, 0, 0};
}

void UnicodeScreen::set_chainfile(const std::string& chainfile, int filesize) {
    for (int i = 0; i < filesize; i++) chainVec.push_back("-");
    if (chainfile.empty()) return;
    std::ifstream file(chainfile);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        int index; std::string chainlist;
        iss >> index >> chainlist;
        if (index >= filesize) continue;
        chainVec[index] = chainlist;
    }
}

void UnicodeScreen::set_utmatrix(const std::string& utmatrix, bool applyUT) {
    yesUT = !utmatrix.empty();
    const size_t filenum = data.size();
    float** matrixpointer = new float*[filenum];
    for (size_t i = 0; i < filenum; i++) {
        matrixpointer[i] = new float[9];
        for (int j = 0; j < 9; j++)
            matrixpointer[i][j] = (j % 4 == 0) ? 1.f : 0.f;
    }
    if (utmatrix.empty()) { for (size_t i = 0; i < filenum; i++) delete[] matrixpointer[i]; delete[] matrixpointer; return; }
    std::ifstream file(utmatrix);
    if (!file.is_open()) { for (size_t i = 0; i < filenum; i++) delete[] matrixpointer[i]; delete[] matrixpointer; return; }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        int index; std::string mat9Str, mat3Str;
        iss >> index >> mat9Str >> mat3Str;
        if (index < 0 || index >= (int)filenum) continue;
        { std::istringstream mss(mat9Str); std::string val; int c = 0;
          while (std::getline(mss, val, ',') && c < 9) matrixpointer[index][c++] = std::stof(val); }
        { std::istringstream mss(mat3Str); std::string val; int c = 0;
          while (std::getline(mss, val, ',') && c < 3) vectorpointer[index][c++] = std::stof(val); }
    }
    if (applyUT) {
        for (size_t i = 0; i < filenum; i++) {
            data[i]->do_naive_rotation(matrixpointer[i]);
            data[i]->do_shift(vectorpointer[i]);
        }
    }
    for (size_t i = 0; i < filenum; i++) delete[] matrixpointer[i];
    delete[] matrixpointer;
}

void UnicodeScreen::normalize_proteins(const std::string& utmatrix) {
    const bool hasUT = !utmatrix.empty();
    for (size_t i = 0; i < data.size(); i++)
        data[i]->load_data(vectorpointer[i], yesUT);
    if (hasUT) set_utmatrix(utmatrix, true);

    global_bb = BoundingBox();
    for (auto* p : data) { p->set_bounding_box(); global_bb = global_bb + p->get_bounding_box(); }

    float max_ext = std::max({global_bb.max_x - global_bb.min_x,
                              global_bb.max_y - global_bb.min_y,
                              global_bb.max_z - global_bb.min_z});
    float scale = (max_ext > 0.f) ? (2.0f / max_ext) : 1.0f;

    if (hasUT) {
        float gx = 0.5f * (global_bb.min_x + global_bb.max_x);
        float gy = 0.5f * (global_bb.min_y + global_bb.max_y);
        float gz = 0.5f * (global_bb.min_z + global_bb.max_z);
        float global_shift[3] = {-gx, -gy, -gz};
        for (auto* p : data) { p->set_scale(scale); p->do_shift(global_shift); p->do_scale(scale); }
    } else {
        for (auto* p : data) {
            float cs[3] = {-p->cx, -p->cy, -p->cz};
            p->set_scale(scale); p->do_shift(cs); p->do_scale(scale);
        }
    }

    query_terminal_size();
    framebuffer.resize(buf_width * buf_height, {0, 0, 0, 0.0f, false});
}

// --- Pixel operations ---

void UnicodeScreen::clear_framebuffer() {
    std::fill(framebuffer.begin(), framebuffer.end(), Pixel{0, 0, 0, 0.0f, false});
}

RGB UnicodeScreen::depth_shade(RGB color, float brightness) {
    brightness = std::clamp(brightness, 0.45f, 1.0f);
    return {
        (uint8_t)(color.r * brightness),
        (uint8_t)(color.g * brightness),
        (uint8_t)(color.b * brightness),
    };
}

void UnicodeScreen::plot_pixel(int x, int y, float z, RGB color, float brightness) {
    if (x < 0 || x >= buf_width || y < 0 || y >= buf_height) return;
    int idx = y * buf_width + x;
    if (framebuffer[idx].active && z > framebuffer[idx].depth + 0.01f) return;

    RGB shaded = depth_shade(color, brightness);
    framebuffer[idx] = {shaded.r, shaded.g, shaded.b, z, true};
}

// --- Drawing primitives ---

void UnicodeScreen::draw_line(int x0, int y0, float z0,
                               int x1, int y1, float z1,
                               RGB color, float brightness) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = std::max(abs(dx), abs(dy));
    if (steps == 0) { plot_pixel(x0, y0, z0, color, brightness); return; }

    float xInc = (float)dx / steps;
    float yInc = (float)dy / steps;
    float zInc = (z1 - z0) / steps;
    float x = (float)x0, y = (float)y0, z = z0;

    int thick = use_sixel ? 2 : 1;

    for (int i = 0; i <= steps; i++) {
        int ix = (int)(x + 0.5f);
        int iy = (int)(y + 0.5f);
        plot_pixel(ix, iy, z, color, brightness);
        for (int t = 1; t <= thick; t++) {
            float fade = brightness * (1.0f - 0.25f * t);
            plot_pixel(ix + t, iy, z, color, fade);
            plot_pixel(ix - t, iy, z, color, fade);
            plot_pixel(ix, iy + t, z, color, fade);
            plot_pixel(ix, iy - t, z, color, fade);
        }
        x += xInc; y += yInc; z += zInc;
    }
}

void UnicodeScreen::draw_filled_circle(int cx, int cy, float z, int radius,
                                        RGB color, float brightness) {
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            float dist = sqrtf((float)(dx * dx + dy * dy));
            if (dist <= radius) {
                float edge = 1.0f - std::max(0.0f, (dist - radius + 1.5f) / 1.5f);
                plot_pixel(cx + dx, cy + dy, z, color, brightness * edge);
            }
        }
    }
}

// --- Color ---

RGB UnicodeScreen::get_color_for_point(int point_idx, int total_points) {
    if (total_points <= 1) return palette_colors[0];
    float t = (float)point_idx / (total_points - 1);
    return interpolate_color(t);
}

// --- Auto-rotation (around atom centroid) ---

void UnicodeScreen::auto_rotate_step() {
    if (!auto_rotate) return;

    float cosA = cosf(rotation_speed);
    float sinA = sinf(rotation_speed);

    for (auto* protein : data) {
        float cx = 0, cy = 0, cz = 0;
        int count = 0;
        for (auto& [chainID, chain_atoms] : protein->get_atoms()) {
            for (Atom& atom : chain_atoms) {
                cx += atom.x; cy += atom.y; cz += atom.z;
                count++;
            }
        }
        if (count == 0) continue;
        cx /= count; cy /= count; cz /= count;

        for (auto& [chainID, chain_atoms] : protein->get_atoms()) {
            for (Atom& atom : chain_atoms) {
                float dx = atom.x - cx;
                float dz = atom.z - cz;
                atom.x = cx + dx * cosA + dz * sinA;
                atom.z = cz - dx * sinA + dz * cosA;
            }
        }
    }
}

// --- Projection helpers ---

struct ProjAtom {
    int sx, sy;
    float z;
    float brightness;
    RGB color;
    int chain_idx;
    int total_chains;
    char ss_type;
    int global_idx;
};

static void project_atoms(std::vector<Protein*>& data,
                           std::vector<float>& pan_x,
                           float zoom_level, float focal_offset,
                           std::vector<float>& pan_y,
                           int buf_width, int buf_height,
                           std::vector<std::vector<ProjAtom>>& chains_out,
                           int& global_total) {
    global_total = 0;

    float cx = 0, cy = 0, cz = 0;
    int count = 0;
    for (auto* p : data) {
        for (const auto& [cid, atoms] : p->get_atoms()) {
            int n = p->get_chain_length(cid);
            for (int i = 0; i < n; i++) {
                float* pos = atoms[i].get_position();
                cx += pos[0]; cy += pos[1]; cz += pos[2];
                count++;
            }
            global_total += n;
        }
    }
    if (count > 0) { cx /= count; cy /= count; cz /= count; }

    int total_chains = 0;
    for (auto* p : data) {
        for (const auto& [cid, atoms] : p->get_atoms())
            total_chains++;
    }

    float fovRads = 1.0f / tanf((FOV / zoom_level) * 0.5f / 180.0f * PI);
    float half_w = buf_width * 0.5f;
    float half_h = buf_height * 0.5f;
    float scale = std::min(half_w, half_h);
    int global_idx = 0;
    int chain_idx = 0;

    for (size_t ii = 0; ii < data.size(); ii++) {
        Protein* target = data[ii];
        for (const auto& [chainID, chain_atoms] : target->get_atoms()) {
            int num_atoms = target->get_chain_length(chainID);
            if (num_atoms == 0) { chain_idx++; continue; }

            std::vector<ProjAtom> chain;
            for (int i = 0; i < num_atoms; i++) {
                float* pos = chain_atoms[i].get_position();
                char ss = chain_atoms[i].get_structure();
                float x = pos[0] - cx, y = pos[1] - cy;
                float z = (pos[2] - cz) + focal_offset;

                float projX = (x / z) * fovRads + pan_x[ii];
                float projY = (y / z) * fovRads + pan_y[ii];
                int sx = (int)(half_w + projX * scale);
                int sy = (int)(half_h - projY * scale);

                float min_z = target->get_scaled_min_z();
                float max_z = target->get_scaled_max_z();
                float zn = (max_z > min_z) ? ((pos[2] - min_z) / (max_z - min_z)) : 0.5f;
                zn = std::clamp(zn, 0.0f, 1.0f);
                float brightness = 1.0f - zn * 0.65f;

                chain.push_back({sx, sy, z, brightness, {0, 0, 0},
                                 chain_idx, total_chains, ss, global_idx});
                global_idx++;
            }
            chains_out.push_back(std::move(chain));
            chain_idx++;
        }
    }
}

// --- View: Backbone ---

void UnicodeScreen::project_backbone() {
    std::vector<std::vector<ProjAtom>> chains;
    int global_total;
    project_atoms(data, pan_x, zoom_level, focal_offset, pan_y,
                  buf_width, buf_height, chains, global_total);

    for (auto& chain : chains) {
        for (size_t i = 0; i < chain.size(); i++) {
            auto& a = chain[i];
            RGB color;
            switch (color_scheme) {
                case ColorScheme::RAINBOW:   color = get_color_for_point(a.global_idx, global_total); break;
                case ColorScheme::CHAIN:     color = get_chain_color(a.chain_idx, a.total_chains); break;
                case ColorScheme::STRUCTURE: color = get_ss_color(a.ss_type); break;
            }
            if (i > 0) {
                draw_line(chain[i-1].sx, chain[i-1].sy, chain[i-1].z,
                          a.sx, a.sy, a.z, color, a.brightness);
            }
        }
    }
}

// --- View: Surface Grid (wireframe mesh) ---

void UnicodeScreen::project_grid() {
    std::vector<std::vector<ProjAtom>> chains;
    int global_total;
    project_atoms(data, pan_x, zoom_level, focal_offset, pan_y,
                  buf_width, buf_height, chains, global_total);

    struct FlatAtom {
        int sx, sy;
        float z, brightness;
        float x3d, y3d, z3d;
        RGB color;
    };
    std::vector<FlatAtom> all_atoms;

    int chain_offset = 0;
    for (size_t ii = 0; ii < data.size(); ii++) {
        Protein* target = data[ii];
        for (const auto& [chainID, chain_atoms] : target->get_atoms()) {
            int num_atoms = target->get_chain_length(chainID);
            for (int i = 0; i < num_atoms; i++) {
                float* pos = chain_atoms[i].get_position();
                if (chain_offset < (int)chains.size() && i < (int)chains[chain_offset].size()) {
                    auto& pa = chains[chain_offset][i];
                    RGB color;
                    switch (color_scheme) {
                        case ColorScheme::RAINBOW:   color = get_color_for_point(pa.global_idx, global_total); break;
                        case ColorScheme::CHAIN:     color = get_chain_color(pa.chain_idx, pa.total_chains); break;
                        case ColorScheme::STRUCTURE: color = get_ss_color(pa.ss_type); break;
                    }
                    all_atoms.push_back({pa.sx, pa.sy, pa.z, pa.brightness,
                                         pos[0], pos[1], pos[2], color});
                }
            }
            chain_offset++;
        }
    }

    float avg_dist = 0;
    int dist_count = 0;
    for (size_t i = 1; i < all_atoms.size(); i++) {
        float dx = all_atoms[i].x3d - all_atoms[i-1].x3d;
        float dy = all_atoms[i].y3d - all_atoms[i-1].y3d;
        float dz = all_atoms[i].z3d - all_atoms[i-1].z3d;
        float d = sqrtf(dx*dx + dy*dy + dz*dz);
        if (d > 0.001f && d < 0.5f) {
            avg_dist += d;
            dist_count++;
        }
    }
    float threshold = (dist_count > 0) ? (avg_dist / dist_count) * 2.5f : 0.15f;

    int n = (int)all_atoms.size();

    int flat_idx = 0;
    for (auto& chain : chains) {
        for (size_t i = 1; i < chain.size(); i++) {
            int ai = flat_idx + (int)i - 1;
            int bi = flat_idx + (int)i;
            if (ai >= 0 && ai < n && bi < n) {
                RGB color = all_atoms[bi].color;
                float br = (all_atoms[ai].brightness + all_atoms[bi].brightness) * 0.5f;
                draw_line(all_atoms[ai].sx, all_atoms[ai].sy, all_atoms[ai].z,
                          all_atoms[bi].sx, all_atoms[bi].sy, all_atoms[bi].z,
                          color, br);
            }
        }
        flat_idx += (int)chain.size();
    }

    for (int i = 0; i < n; i++) {
        for (int j = i + 3; j < n; j++) {
            float dx = all_atoms[i].x3d - all_atoms[j].x3d;
            float dy = all_atoms[i].y3d - all_atoms[j].y3d;
            float dz = all_atoms[i].z3d - all_atoms[j].z3d;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist < threshold) {
                RGB color = all_atoms[j].color;
                float br = (all_atoms[i].brightness + all_atoms[j].brightness) * 0.5f;
                int ddx = all_atoms[j].sx - all_atoms[i].sx;
                int ddy = all_atoms[j].sy - all_atoms[i].sy;
                int steps = std::max(abs(ddx), abs(ddy));
                if (steps == 0) continue;
                float xInc = (float)ddx / steps;
                float yInc = (float)ddy / steps;
                float zInc = (all_atoms[j].z - all_atoms[i].z) / steps;
                float px = (float)all_atoms[i].sx, py = (float)all_atoms[i].sy;
                float pz = all_atoms[i].z;
                for (int s = 0; s <= steps; s++) {
                    plot_pixel((int)(px + 0.5f), (int)(py + 0.5f), pz, color, br * 0.7f);
                    px += xInc; py += yInc; pz += zInc;
                }
            }
        }
    }

    int dot_r = use_sixel ? 3 : 1;
    for (int i = 0; i < n; i++)
        draw_filled_circle(all_atoms[i].sx, all_atoms[i].sy, all_atoms[i].z,
                           dot_r, all_atoms[i].color, all_atoms[i].brightness);
}

// --- View: Surface ---

void UnicodeScreen::project_surface() {
    std::vector<std::vector<ProjAtom>> chains;
    int global_total;
    project_atoms(data, pan_x, zoom_level, focal_offset, pan_y,
                  buf_width, buf_height, chains, global_total);

    float r_scale = use_sixel ? 4.0f : 1.0f;
    for (auto& chain : chains) {
        for (auto& a : chain) {
            RGB color;
            switch (color_scheme) {
                case ColorScheme::RAINBOW:   color = get_color_for_point(a.global_idx, global_total); break;
                case ColorScheme::CHAIN:     color = get_chain_color(a.chain_idx, a.total_chains); break;
                case ColorScheme::STRUCTURE: color = get_ss_color(a.ss_type); break;
            }
            int radius = (int)((3.0f + a.brightness * 3.0f) * r_scale);
            draw_filled_circle(a.sx, a.sy, a.z, radius, color, a.brightness);
        }
    }
}

// --- Braille rendering ---

void UnicodeScreen::render_braille() {
    std::string out;
    out.reserve(term_cols * (term_rows - info_rows) * 30);
    out += "\033[H";

    int cell_rows = buf_height / 4;
    int cell_cols = buf_width / 2;

    static const int dot_bits[2][4] = {
        {0x01, 0x02, 0x04, 0x40},
        {0x08, 0x10, 0x20, 0x80},
    };

    for (int cr = 0; cr < cell_rows; cr++) {
        for (int cc = 0; cc < cell_cols; cc++) {
            int pattern = 0;
            float best_depth = std::numeric_limits<float>::infinity();
            RGB best_color = {0, 0, 0};
            bool any_active = false;

            for (int dc = 0; dc < 2; dc++) {
                for (int dr = 0; dr < 4; dr++) {
                    int px = cc * 2 + dc;
                    int py = cr * 4 + dr;
                    if (px >= buf_width || py >= buf_height) continue;
                    int idx = py * buf_width + px;
                    if (framebuffer[idx].active) {
                        pattern |= dot_bits[dc][dr];
                        any_active = true;
                        if (framebuffer[idx].depth < best_depth) {
                            best_depth = framebuffer[idx].depth;
                            best_color = {framebuffer[idx].r, framebuffer[idx].g, framebuffer[idx].b};
                        }
                    }
                }
            }

            if (any_active) {
                out += "\033[38;2;";
                out += std::to_string(best_color.r) + ";";
                out += std::to_string(best_color.g) + ";";
                out += std::to_string(best_color.b) + "m";
                out += "\033[48;2;";
                out += std::to_string(bg_color.r) + ";";
                out += std::to_string(bg_color.g) + ";";
                out += std::to_string(bg_color.b) + "m";
                int codepoint = 0x2800 + pattern;
                out += (char)(0xE0 | ((codepoint >> 12) & 0x0F));
                out += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                out += (char)(0x80 | (codepoint & 0x3F));
            } else {
                out += "\033[48;2;";
                out += std::to_string(bg_color.r) + ";";
                out += std::to_string(bg_color.g) + ";";
                out += std::to_string(bg_color.b) + "m ";
            }
        }
        if (cr < cell_rows - 1) out += "\033[0m\n";
    }

    out += "\033[0m";
    write(STDOUT_FILENO, out.c_str(), out.size());
}

// --- Sixel rendering ---

void UnicodeScreen::render_sixel() {
    std::vector<RGBA> pixels(buf_width * buf_height);
    for (int i = 0; i < buf_width * buf_height; i++) {
        if (framebuffer[i].active) {
            pixels[i] = {framebuffer[i].r, framebuffer[i].g, framebuffer[i].b, 255};
        } else {
            pixels[i] = {bg_color.r, bg_color.g, bg_color.b, 255};
        }
    }

    std::string out;
    out += "\033[H";
    out += SixelEncoder::encode(pixels, buf_width, buf_height,
                                bg_color.r, bg_color.g, bg_color.b);
    write(STDOUT_FILENO, out.c_str(), out.size());
}

// --- View mode name ---

const char* UnicodeScreen::view_mode_name() {
    switch (view_mode) {
        case ViewMode::BACKBONE: return "backbone";
        case ViewMode::GRID:    return "grid";
        case ViewMode::SURFACE: return "surface";
    }
    return "unknown";
}

// --- Info overlay ---

void UnicodeScreen::draw_info_overlay() {
    std::string out;
    out += "\033[0m\n";

    for (size_t i = 0; i < data.size(); i++) {
        auto* p = data[i];
        std::string name = p->get_file_name();
        size_t slash = name.find_last_of('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);

        RGB nc = interpolate_color((float)i / std::max(1, (int)data.size() - 1));
        out += "\033[38;2;" + std::to_string(nc.r) + ";" +
               std::to_string(nc.g) + ";" +
               std::to_string(nc.b) + "m";
        out += " " + name;

        out += "\033[38;2;" + std::to_string(fg_color.r * 2 / 3) + ";" +
               std::to_string(fg_color.g * 2 / 3) + ";" +
               std::to_string(fg_color.b * 2 / 3) + "m";

        auto chain_lengths = p->get_chain_length();
        auto residue_counts = p->get_residue_count();
        int total_res = 0, total_chains = 0;
        for (const auto& [cid, len] : chain_lengths) {
            total_chains++;
            auto it = residue_counts.find(cid);
            if (it != residue_counts.end()) total_res += it->second;
        }
        out += "  " + std::to_string(total_chains) + " chain" +
               (total_chains > 1 ? "s" : "") + ", " +
               std::to_string(total_res) + " residues";

        out += "  \033[38;2;" + std::to_string(fg_color.r / 3) + ";" +
               std::to_string(fg_color.g / 3) + ";" +
               std::to_string(fg_color.b / 3) + "m";
        out += "[" + std::string(view_mode_name()) + "]";
        out += " [" + std::string(color_scheme_name()) + "]";
        out += " [" + std::string(palette_name()) + "]";

        out += "\033[0m";
        if (i < data.size() - 1) out += "\n";
    }

    write(STDOUT_FILENO, out.c_str(), out.size());
}

// --- Left sidebar with protein info ---

static std::vector<std::string> word_wrap(const std::string& text, int width) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string word, line;
    while (stream >> word) {
        if (line.empty()) {
            line = word;
        } else if ((int)(line.size() + 1 + word.size()) <= width) {
            line += " " + word;
        } else {
            lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void UnicodeScreen::draw_sidebar() {
    if (data.empty()) return;
    auto* p = data[0];

    std::string title = p->get_title();
    std::string pid = p->get_pdb_id();

    auto chain_lengths = p->get_chain_length();
    auto residue_counts = p->get_residue_count();
    int total_res = 0, total_chains = 0;
    for (const auto& [cid, len] : chain_lengths) {
        total_chains++;
        auto it = residue_counts.find(cid);
        if (it != residue_counts.end()) total_res += it->second;
    }

    int sidebar_w = std::clamp(term_cols / 3, 20, 40);
    int content_w = sidebar_w - 3;
    int max_rows = term_rows - info_rows - 2;

    std::vector<std::pair<std::string, RGB>> lines;

    RGB accent = palette_colors.empty() ? fg_color : palette_colors[0];
    RGB dim_fg = {(uint8_t)(fg_color.r * 2 / 3),
                  (uint8_t)(fg_color.g * 2 / 3),
                  (uint8_t)(fg_color.b * 2 / 3)};
    RGB dim2_fg = {(uint8_t)(fg_color.r / 2),
                   (uint8_t)(fg_color.g / 2),
                   (uint8_t)(fg_color.b / 2)};

    if (!pid.empty())
        lines.push_back({pid, accent});

    std::string stats = std::to_string(total_chains) + " chain" +
                        (total_chains != 1 ? "s" : "") + ", " +
                        std::to_string(total_res) + " residues";
    lines.push_back({stats, dim_fg});

    lines.push_back({"", dim_fg});

    if (!title.empty()) {
        auto wrapped = word_wrap(title_case(title), content_w);
        for (auto& l : wrapped)
            lines.push_back({l, dim_fg});
    }

    if (!sidebar_info.empty()) {
        lines.push_back({"", dim2_fg});
        for (const auto& info_line : sidebar_info) {
            if (info_line.empty()) {
                lines.push_back({"", dim2_fg});
            } else {
                auto wrapped = word_wrap(info_line, content_w);
                for (auto& l : wrapped)
                    lines.push_back({l, dim2_fg});
            }
        }
    }

    lines.push_back({"", dim2_fg});
    lines.push_back({"[c] " + std::string(color_scheme_name()), dim2_fg});
    lines.push_back({"[p] " + std::string(palette_name()), dim2_fg});

    std::string out;
    std::string bg_esc = "\033[48;2;" + std::to_string(bg_color.r) + ";" +
                         std::to_string(bg_color.g) + ";" +
                         std::to_string(bg_color.b) + "m";

    int start_row = 2;
    int num_lines = std::min((int)lines.size(), max_rows);

    for (int i = 0; i < num_lines; i++) {
        int row = start_row + i;
        out += "\033[" + std::to_string(row) + ";1H";
        out += bg_esc;

        RGB c = lines[i].second;
        out += "\033[38;2;" + std::to_string(c.r) + ";" +
               std::to_string(c.g) + ";" + std::to_string(c.b) + "m";

        std::string padded = "  " + lines[i].first;
        if ((int)padded.size() < sidebar_w)
            padded.append(sidebar_w - padded.size(), ' ');
        else
            padded = padded.substr(0, sidebar_w);
        out += padded;
    }

    out += "\033[0m";
    write(STDOUT_FILENO, out.c_str(), out.size());
}

// --- Main draw ---

void UnicodeScreen::draw_screen() {
    int old_w = buf_width, old_h = buf_height;
    query_terminal_size();
    if (buf_width != old_w || buf_height != old_h)
        framebuffer.resize(buf_width * buf_height);

    auto_rotate_step();
    clear_framebuffer();

    switch (view_mode) {
        case ViewMode::BACKBONE: project_backbone(); break;
        case ViewMode::GRID:     project_grid();     break;
        case ViewMode::SURFACE:  project_surface();  break;
    }

    if (use_sixel)
        render_sixel();
    else
        render_braille();
    draw_info_overlay();
    draw_sidebar();
}

// --- Input handling ---

bool UnicodeScreen::handle_input() {
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    if (poll(&pfd, 1, 0) <= 0) return true;

    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return true;

    float pan_step = 0.05f;

    switch (c) {
        case '0': structNum = -1; break;
        case '1': case '2': case '3': case '4': case '5': case '6':
            if (c - '0' <= (int)data.size()) structNum = c - '1';
            break;
        case 'v': case 'V': {
            int m = (int)view_mode;
            m = (m + 1) % 3;
            view_mode = (ViewMode)m;
            break;
        }
        case 'a': case 'A':
            if (structNum >= 0) pan_x[structNum] -= pan_step;
            else for (auto& px : pan_x) px -= pan_step;
            break;
        case 'd': case 'D':
            if (structNum >= 0) pan_x[structNum] += pan_step;
            else for (auto& px : pan_x) px += pan_step;
            break;
        case 'w': case 'W':
            if (structNum >= 0) pan_y[structNum] += pan_step;
            else for (auto& py : pan_y) py += pan_step;
            break;
        case 's': case 'S':
            if (structNum >= 0) pan_y[structNum] -= pan_step;
            else for (auto& py : pan_y) py -= pan_step;
            break;
        case 'x': case 'X':
            if (structNum >= 0) data[structNum]->set_rotate(1, 0, 0);
            else for (auto* p : data) p->set_rotate(1, 0, 0);
            break;
        case 'y': case 'Y':
            if (structNum >= 0) data[structNum]->set_rotate(0, 1, 0);
            else for (auto* p : data) p->set_rotate(0, 1, 0);
            break;
        case 'z': case 'Z':
            if (structNum >= 0) data[structNum]->set_rotate(0, 0, 1);
            else for (auto* p : data) p->set_rotate(0, 0, 1);
            break;
        case 'r': case 'R':
            if (zoom_level + 0.3f < 15.0f) zoom_level += 0.3f;
            break;
        case 'f': case 'F':
            if (zoom_level - 0.3f > 0.5f) zoom_level -= 0.3f;
            break;
        case ' ':
            auto_rotate = !auto_rotate;
            break;
        case 'c': case 'C': {
            int s = (int)color_scheme;
            s = (s + 1) % 3;
            color_scheme = (ColorScheme)s;
            break;
        }
        case 'p': case 'P': {
            int pt = (int)palette_type;
            pt = (pt + 1) % (int)PaletteType::PALETTE_COUNT;
            palette_type = (PaletteType)pt;
            apply_palette();
            break;
        }
        case 'n': case 'N':
            if (random_mode) load_random_pdb();
            break;
        case 'q': case 'Q':
            return false;
    }
    return true;
}
