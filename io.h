#include <array>
#include <string>
#include <vector>

#include "gui.h"


bool write_file(const std::string & file_name, const std::array<std::vector<clickable>, pattern_groups> & data, const int bpm, const std::array<sample, pattern_groups> & sample_files);
bool read_file (const std::string & file_name, std::array<std::vector<clickable>, pattern_groups> *const data, int *const bpm, std::array<sample, pattern_groups> *const sample_files);
std::string get_filename(const std::string & path);
