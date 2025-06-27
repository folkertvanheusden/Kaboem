#include <array>
#include <string>
#include <vector>

#include "gui.h"
#include "sound.h"

struct file_parameter
{
	std::string            name;
	bool                   is_float { false };
	int                   *i_value  { 0     };
	std::optional<int>    *oi_value;
	double                *d_value  { 0     };
	std::optional<double> *od_value;
};

bool write_file(const std::string & file_name, const std::array<pattern, pattern_groups> & data, const std::array<sample, pattern_groups> & sample_files,
		const std::vector<file_parameter> & parameters);
bool read_file (const std::string & file_name, std::array<pattern, pattern_groups> *const data, std::array<sample, pattern_groups> *const sample_files,
		const std::vector<file_parameter> *const parameters);
std::string get_filename(const std::string & path);
sound_sample *find_sample(const std::vector<std::string> & search_paths, const std::string & file_name);
