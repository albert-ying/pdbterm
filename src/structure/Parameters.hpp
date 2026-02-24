#pragma once
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

class Parameters{
    private:
        bool show_structure = false;
        bool predict_structure = false;
        bool sixel = false;
        bool random_pdb = false;
        bool arg_okay = true;
        vector<string> in_file;
        vector<string> chains;
        string utmatrix = "";
        string chainfile = "";
        string mode = "protein";
        string pdb_id = "";
        string render_path = "";
    public:
        Parameters(int argc, char* argv[]);

        void print_args();

        bool is_valid_number(const std::string& str, int min, int max);

        // get, set
        vector<string>& get_in_file(){
            return in_file;
        }
        string get_in_file(int idx){
            if (idx < in_file.size()){
                return in_file[idx];
            }
            return "";
        }
        string get_chainfile(){
            return chainfile;
        }
        string get_utmatrix(){
            return utmatrix;
        }
        string get_mode(){
            return mode;
        }
        bool get_show_structure(){
            return show_structure;
        }
        bool check_arg_okay(){
            return arg_okay;
        }
        bool get_sixel(){
            return sixel;
        }
        bool get_random_pdb(){
            return random_pdb;
        }
        string get_pdb_id(){
            return pdb_id;
        }
        string get_render_path(){
            return render_path;
        }
};
