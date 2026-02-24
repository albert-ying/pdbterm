#include "Parameters.hpp"
#include <cmath>

static void print_help(){
    std::cout << "pdbterm — Terminal protein structure viewer\n\n";
    std::cout << "Usage:\n";
    std::cout << "  pdbterm <file.pdb|file.cif> [options]\n";
    std::cout << "  pdbterm --pdb <ID>           Fetch and display a PDB structure by ID\n";
    std::cout << "  pdbterm --random             Fetch and display a random PDB structure\n\n";
    std::cout << "Options:\n";
    std::cout << "  -m, --mode <mode>    Color mode: protein (default), chain, rainbow\n";
    std::cout << "  -s, --structure      Show secondary structure (alpha helix, beta sheet)\n";
    std::cout << "  -p, --predict        Predict secondary structure if not in input file\n";
    std::cout << "  -c, --chains <file>  Show only selected chains (see example/chainfile)\n";
    std::cout << "  --sixel              Render using Sixel graphics (requires Sixel-capable terminal)\n";
    std::cout << "  --render <path>      Render a PNG screenshot and exit (headless, 1280x720)\n";
    std::cout << "  --help               Show this help message\n\n";
    std::cout << "Interactive controls:\n";
    std::cout << "  Arrow keys / WASD   Pan the view\n";
    std::cout << "  x / y / z           Rotate around axis\n";
    std::cout << "  r / f               Zoom in / out\n";
    std::cout << "  v                   Cycle view mode (backbone/grid/surface)\n";
    std::cout << "  c                   Cycle color scheme (rainbow/chain/structure)\n";
    std::cout << "  p                   Cycle palette (neon/cool/warm/earth/pastel)\n";
    std::cout << "  Space               Toggle auto-rotation\n";
    std::cout << "  n                   Next random structure (--random mode)\n";
    std::cout << "  q                   Quit\n";
}

Parameters::Parameters(int argc, char* argv[]) {
    arg_okay = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            print_help();
            std::exit(0);
        }
    }

    if (argc <= 1) {
        std::cerr << "Error: Need input file, --pdb <ID>, or --random\n";
        std::cerr << "Run 'pdbterm --help' for usage information.\n";
        arg_okay = false;
        return;
    }

    for (int i = 1; i < argc; i++) {
        try {
            if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--mode")) {
                if (i + 1 < argc) {
                    std::string val(argv[i + 1]);
                    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                    if (val == "chain" || val == "rainbow" || val == "protein") {
                        mode = val;
                        i++;
                    } else {
                        throw std::runtime_error("Error: Invalid value for --mode. Use 'protein', 'chain' or 'rainbow'.");
                    }
                } else {
                    throw std::runtime_error("Error: Missing value for -m / --mode.");
                }
            }
            else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--chains")) {
                if (i + 1 < argc) {
                    chainfile = argv[++i];
                } else {
                    throw std::runtime_error("Error: Missing value for -c / --chains.");
                }
            }
            else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--structure")) {
                show_structure = true;
            }
            else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--predict")) {
                predict_structure = true;
            }
            else if (!strcmp(argv[i], "--sixel")) {
                sixel = true;
            }
            else if (!strcmp(argv[i], "--random")) {
                random_pdb = true;
            }
            else if (!strcmp(argv[i], "--pdb")) {
                if (i + 1 < argc) {
                    pdb_id = argv[++i];
                    // Uppercase the PDB ID
                    std::transform(pdb_id.begin(), pdb_id.end(), pdb_id.begin(), ::toupper);
                } else {
                    throw std::runtime_error("Error: Missing value for --pdb.");
                }
            }
            else if (!strcmp(argv[i], "--render")) {
                if (i + 1 < argc) {
                    render_path = argv[++i];
                } else {
                    throw std::runtime_error("Error: Missing value for --render.");
                }
            }
            else if (!strcmp(argv[i], "-ut") || !strcmp(argv[i], "--utmatrix")) {
                if (i + 1 < argc) {
                    utmatrix = argv[++i];
                } else {
                    throw std::runtime_error("Error: Missing value for -ut / --utmatrix.");
                }
            } else if (fs::exists(argv[i]) && fs::is_regular_file(argv[i]) && in_file.size() < 6){
                in_file.push_back(argv[i]);
            }
            else {
                throw std::runtime_error("Error: Unknown parameter: " + std::string(argv[i]));
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Wrong input parameters: " << e.what() << std::endl;
            std::cerr << "Error at argument: " << argv[i] << std::endl;
            arg_okay = false;
            return;
        }
    }
    while(in_file.size() != chains.size()){
        chains.push_back("-");
    }

    // Validate: --random and --pdb are mutually exclusive
    if (random_pdb && !pdb_id.empty()) {
        std::cerr << "Error: --random and --pdb are mutually exclusive." << std::endl;
        arg_okay = false;
        return;
    }

    // Need at least one input source
    if (in_file.size() == 0 && !random_pdb && pdb_id.empty()){
        std::cerr << "Error: Need input file, --pdb <ID>, or --random" << std::endl;
        arg_okay = false;
        return;
    }
    return;
}

void Parameters::print_args() {
    cout << "Input parameters >> " << endl;
    if (!pdb_id.empty()) {
        cout << "  pdb_id: " << pdb_id << endl;
    }
    if (!in_file.empty()) {
        cout << "  in_file: " << endl;
        for (int i = 0; i < in_file.size(); i++) {
            std::cout << "\t" << in_file[i] << ": " << chains[i] << '\n';
        }
    }
    cout << "  mode: " << mode << endl;
    cout << "  utmatrix: " << utmatrix << endl;
    cout << "  chainfile: " << chainfile << endl;
    cout << "  show_structure: " << show_structure << endl;
    cout << "  sixel: " << sixel << endl;
    cout << "  random: " << random_pdb << endl;
    if (!render_path.empty()) {
        cout << "  render: " << render_path << endl;
    }
    cout << "\n";
    return;
}
