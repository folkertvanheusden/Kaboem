#include <fstream>
#include <nlohmann/json.hpp>

#include "gui.h"

using json = nlohmann::json;


std::string get_dirname(const std::string & path)
{
	auto slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return "./";
	return path.substr(0, slash);
}

std::string get_filename(const std::string & path)
{
	auto slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return path;
	return path.substr(slash + 1);
}

bool write_file(const std::string & file_name, const std::array<std::vector<clickable>, pattern_groups> & data, const int bpm, const std::array<sample, pattern_groups> & sample_files)
{
	json patterns = json::array();
	for(auto & group: data) {
		json group_pattern = json::array();

		for(auto & element: group)
			group_pattern.push_back(element.selected);

		patterns.push_back(group_pattern);
	}

	std::string dir = get_dirname(file_name);
	json samples = json::array();
	for(auto & sample_file : sample_files) {
		std::string compare_dir = sample_file.name.substr(0, std::min(dir.size(), sample_file.name.size()));
		if (compare_dir != dir)
			samples.push_back(sample_file.name);
		else
			samples.push_back(get_filename(sample_file.name));
	}

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

sound_sample *find_sample(const std::vector<std::string> & search_paths, const std::string & file_name)
{
	for(auto & path: search_paths) {
		sound_sample *s = new sound_sample(sample_rate, path + "/" + file_name);
		if (s->begin())
			return s;
		delete s;
	}
	return nullptr;
}

bool read_file(const std::string & file_name, std::array<std::vector<clickable>, pattern_groups> *const data, int *const bpm, std::array<sample, pattern_groups> *const sample_files)
{
	try {
		std::ifstream ifs(file_name);
		if (ifs.is_open() == false)
			return false;
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
				std::vector<std::string> search_paths { "./", get_dirname(file_name), get_current_dir_name() };
				s.s = find_sample(search_paths, s.name);
				if (!s.s)
					return false;
				s.s->add_mapping(0, 0, 1.0);  // mono -> left
				s.s->add_mapping(0, 1, 1.0);  // mono -> right
			}
		}

		return true;
	}
	catch(const std::ifstream::failure & e) {
		printf("Cannot access %s\n", file_name.c_str());
	}
	catch(const json::parse_error & pe) {
		printf("File %s is incorrect\n", file_name.c_str());
	}

	return false;
}
