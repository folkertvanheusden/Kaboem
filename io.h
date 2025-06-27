#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "gui.h"
#include "sound.h"

struct file_parameter
{
	std::string            name;
	enum { T_INT, T_FLOAT, T_BOOL, T_ABOOL } type { T_INT };
	int                   *i_value  { nullptr };
	std::optional<int>    *oi_value { nullptr };
	double                *d_value  { nullptr };
	std::optional<double> *od_value { nullptr };
	bool                  *b_value  { nullptr };
	std::atomic_bool      *ab_value { nullptr };
};

bool write_file(const std::string & file_name, const std::array<pattern, pattern_groups> & data, const std::array<sample, pattern_groups> & sample_files,
		const std::vector<file_parameter> & parameters);
bool read_file (const std::string & file_name, std::array<pattern, pattern_groups> *const data, std::array<sample, pattern_groups> *const sample_files,
		const std::vector<file_parameter> *const parameters);
std::string get_filename(const std::string & path);
sound_sample *find_sample(const std::vector<std::string> & search_paths, const std::string & file_name);
