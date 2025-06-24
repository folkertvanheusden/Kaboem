#include <filesystem>
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

	std::filesystem::path from     = file_name;
	std::filesystem::path from_dir = from.parent_path();

	json samples          = json::array();
	json sample_vol_left  = json::array();
	json sample_vol_right = json::array();
	json midi_notes       = json::array();
	for(auto & sample_file : sample_files) {
		try {
			std::string filename = std::filesystem::relative(sample_file.name, from_dir);
			samples.push_back(filename);
		}
		// fs::relative throws filesystem_error if paths don't share a common prefix
		catch(std::filesystem::filesystem_error & fe) {
			samples.push_back(sample_file.name);
		}

		if (sample_file.s) {
			sample_vol_left. push_back(sample_file.s->get_mapping_target_volume(0));
			sample_vol_right.push_back(sample_file.s->get_mapping_target_volume(1));
		}
		else {
			sample_vol_left. push_back(0.);
			sample_vol_right.push_back(0.);
		}

		if (sample_file.midi_note.has_value())
			midi_notes.push_back(sample_file.midi_note.value());
		else
			midi_notes.push_back(-1);
	}

	json out;
	out["bpm"]              = bpm;
	out["patterns"]         = patterns;
	out["samples"]          = samples;
	out["sample-vol-left"]  = sample_vol_left;
	out["sample-vol-right"] = sample_vol_right;
	out["midi-notes"]       = midi_notes;

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
			s.s = nullptr;

			if (j.contains("midi-notes")) {
				int note = j["midi-notes"][group];
				if (note != -1)
					s.midi_note = note;
			}

			if (s.name.empty() == false) {
				s.s = new sound_sample(sample_rate, s.name);
				if (s.s->begin() == false) {
					delete s.s;
					s.s = nullptr;
				}
				if (!s.s)
					return false;
				bool is_stereo = s.s->get_n_channels() >= 2;
				if (j.contains("sample-vol-left")) {
					s.s->add_mapping(0, 0, j["sample-vol-left"][group]);
					if (is_stereo)
						s.s->add_mapping(1, 1, j["sample-vol-right"][group]);
					else
						s.s->add_mapping(0, 1, 1.0);  // mono -> right
				}
				else {
					s.s->add_mapping(0, 0, 1.0);
					if (is_stereo)
						s.s->add_mapping(0, 1, 1.0);  // mono -> right
					else
						s.s->add_mapping(1, 1, 1.0);
				}
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
