#include <array>
#include <string>
#include <vector>

#include "gui.h"


bool write_file(const std::string & file_name, const std::array<std::vector<clickable>, pattern_groups> & data, const int bpm);
bool read_file (const std::string & file_name, std::array<std::vector<clickable>, pattern_groups> *const data, int *const bpm);
