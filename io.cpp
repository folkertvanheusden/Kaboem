#include <fstream>
#include <nlohmann/json.hpp>

#include "gui.h"

using json = nlohmann::json;


bool write_file(const std::string & file_name, const std::array<std::vector<clickable>, pattern_groups> & data, const int bpm)
{
	json patterns = json::array();
	for(auto & group: data) {
		json group_pattern = json::array();

		for(auto & element: group)
			group_pattern.push_back(element.selected);

		patterns.push_back(group_pattern);
	}

	json out;
	out["bpm"]      = bpm;
	out["patterns"] = patterns;

	try {
		std::ofstream o(file_name);
		o << out;

		return true;
	}
	catch(...) {
	}

	return false;
}

bool read_file(const std::string & file_name, std::array<std::vector<clickable>, pattern_groups> *const data, int *const bpm)
{
	try {
		std::ifstream ifs(file_name);
		json j = json::parse(ifs);

		*bpm = j["bpm"];

		for(size_t group=0; group<pattern_groups; group++) {
			auto & group_vector = (*data)[group];
			size_t index        = 0;
			for(auto & element: j["patterns"][group])
				group_vector[index++].selected = element;
		}

		return true;
	}
	catch(...) {
	}

	return false;
}
