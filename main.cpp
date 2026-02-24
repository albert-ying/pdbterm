#include <iostream>
#include <unistd.h>
#include "Parameters.hpp"
#include "UnicodeScreen.hpp"

int main(int argc, char* argv[]) {
    Parameters params(argc, argv);

    if (!params.check_arg_okay()) {
        return -1;
    }
    params.print_args();

    bool use_sixel = params.get_sixel();
    UnicodeScreen screen(params.get_show_structure(), params.get_mode(), use_sixel);

    if (!params.get_pdb_id().empty()) {
        // Fetch specific PDB by ID
        std::cout << "Fetching PDB " << params.get_pdb_id() << "..." << std::endl;
        if (!screen.load_specific_pdb(params.get_pdb_id())) {
            std::cerr << "Error: Could not fetch PDB " << params.get_pdb_id()
                      << ". Check the ID and your internet connection." << std::endl;
            return -1;
        }
    } else if (params.get_random_pdb()) {
        // Fetch random PDB
        screen.set_random_mode(true);
        std::cout << "Fetching random PDB structure..." << std::endl;
        if (!screen.load_random_pdb()) {
            std::cerr << "Error: Could not fetch a PDB structure. Check your internet connection." << std::endl;
            return -1;
        }
    } else {
        // Load from local file(s)
        screen.set_chainfile(params.get_chainfile(), params.get_in_file().size());
        for (size_t i = 0; i < params.get_in_file().size(); i++) {
            screen.set_protein(params.get_in_file(i), i, params.get_show_structure());
        }
        screen.set_tmatrix();
        if (!params.get_utmatrix().empty()) {
            screen.set_utmatrix(params.get_utmatrix(), false);
        }
        screen.normalize_proteins(params.get_utmatrix());
    }

    // Headless render mode
    if (!params.get_render_path().empty()) {
        if (screen.write_framebuffer_png(params.get_render_path())) {
            std::cout << "Screenshot saved to " << params.get_render_path() << std::endl;
            return 0;
        } else {
            std::cerr << "Error: Failed to write screenshot to " << params.get_render_path() << std::endl;
            return -1;
        }
    }

    // Interactive mode
    screen.enter_raw_mode();
    bool run = true;
    while (run) {
        screen.draw_screen();
        run = screen.handle_input();
        usleep(33000); // ~30 FPS
    }
    screen.exit_raw_mode();

    return 0;
}
