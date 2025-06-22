#include <fstream>
#include <nlohmann/json.hpp>

#include "gui.h"

using json = nlohmann::json;


bool write_file(const std::string & file_name, const std::array<std::vector<clickable>, pattern_groups> & data, const int bpm, const std::array<sample, pattern_groups> & sample_files)
{
	json patterns = json::array();
	for(auto & group: data) {
		json group_pattern = json::array();

		for(auto & element: group)
			group_pattern.push_back(element.selected);

		patterns.push_back(group_pattern);
	}

	json samples = json::array();
	for(auto & sample_file : sample_files)
		samples.push_back(sample_file.name);

	json out;
	out["bpm"]      = bpm;
	out["patterns"] = patterns;
	out["samples"]  = samples;

	try {
		std::ofstream o(file_name);
		o.exceptions(std::ifstream::badbit);
		o << out;

		return true;
	}
	catch(const std::ifstream::failure& e) {
		printf("Cannot access %s\n", file_name.c_str());
	}

	return false;
}

bool read_file(const std::string & file_name, std::array<std::vector<clickable>, pattern_groups> *const data, int *const bpm, std::array<sample, pattern_groups> *const sample_files)
{
	try {
		std::ifstream ifs(file_name);
		ifs.exceptions(std::ifstream::badbit);

		json j = json::parse(ifs);

		*bpm = j["bpm"];

		for(size_t group=0; group<pattern_groups; group++) {
			auto & group_vector = (*data)[group];
			size_t index        = 0;
			for(auto & element: j["patterns"][group])
				group_vector[index++].selected = element;
		}

		for(size_t group=0; group<pattern_groups; group++) {
			sample & s = (*sample_files)[group];
			s.name = j["samples"][group];
			delete s.s;
			if (s.name.empty())
				s.s = nullptr;
			else {
				s.s    = new sound_sample(sample_rate, s.name);
				s.s->add_mapping(0, 0, 1.0);  // mono -> left
				s.s->add_mapping(0, 1, 1.0);  // mono -> right
			}
		}

		return true;
	}
	catch(const std::ifstream::failure& e) {
		printf("Cannot access %s\n", file_name.c_str());
	}

	return false;
}

std::string get_filename(const std::string & path)
{
	auto slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return path;
	return path.substr(slash + 1);
}
