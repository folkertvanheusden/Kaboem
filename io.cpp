#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "gui.h"
#include "io.h"

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

bool write_file(const std::string & file_name, const std::array<pattern, pattern_groups> & data, const std::array<sample, pattern_groups> & sample_files,
		const std::vector<file_parameter> & parameters)
{
	json patterns = json::array();
	for(auto & group: data) {
		json group_pattern      = json::array();
		for(auto & element: group.pattern)
			group_pattern.push_back(element.selected);
		json group_note_delta   = json::array();
		for(auto & element: group.note_delta)
			group_note_delta.push_back(element);
		json group_volume_left  = json::array();
		for(auto & element: group.volume_left)
			group_volume_left.push_back(element);
		json group_volume_right = json::array();
		for(auto & element: group.volume_right)
			group_volume_right.push_back(element);

		json pattern_data;
		pattern_data["dim"]          = group.dim;
		pattern_data["pattern"]      = group_pattern;
		pattern_data["note-delta"]   = group_note_delta;
		pattern_data["volume-left"]  = group_volume_left;
		pattern_data["volume-right"] = group_volume_right;

		patterns.push_back(pattern_data);
	}

	json samples    = json::array();
	json midi_notes = json::array();
	for(auto & sample_file : sample_files) {
		json sample;
		sample["file-name"] = sample_file.name;

		if (sample_file.s) {
			sample["vol-left"]    = sample_file.s->get_mapping_target_volume(0);
			if (sample_file.s->get_n_channels() >= 2)
				sample["vol-right"] = sample_file.s->get_mapping_target_volume(1);
			else
				sample["vol-right"] = sample_file.s->get_mapping_target_volume(0);
			sample["pitch"]       = sample_file.s->get_pitch_bend();
			sample["mute"]        = sample_file.s->get_mute();

			const std::vector<std::vector<double> > & sample_data = sample_file.s->get_raw();
			sample["data"]        = sample_data;
			sample["sample-rate"] = sample_file.s->get_sample_rate();
		}
		else {
			sample["vol-left"]    = 0.;
			sample["vol-right"]   = 0.;
			sample["pitch"]       = 1.;
			sample["mute"]        = false;
		}

		samples.push_back(sample);

		if (sample_file.midi_note.has_value())
			midi_notes.push_back(sample_file.midi_note.value());
		else
			midi_notes.push_back(-1);
	}

	json out;
	out["patterns"]         = patterns;
	out["samples"]          = samples;
	out["midi-notes"]       = midi_notes;

	for(auto & element: parameters) {
		if (element.type == file_parameter::T_FLOAT) {
			if (element.d_value)
				out[element.name] = *element.d_value;
			else if (element.od_value->has_value())
				out[element.name] = element.od_value->value();
		}
		else if (element.type == file_parameter::T_INT) {
			if (element.i_value)
				out[element.name] = *element.i_value;
			else if (element.oi_value->has_value())
				out[element.name] = element.oi_value->value();
		}
		else if (element.type == file_parameter::T_BOOL)
			out[element.name] = *element.b_value;
		else if (element.type == file_parameter::T_ABOOL) {
			out[element.name] = bool(*element.ab_value);
		}
	}

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

bool read_file(const std::string & file_name, std::array<pattern, pattern_groups> *const data, std::array<sample, pattern_groups> *const sample_files,
		const std::vector<file_parameter> *const parameters)
{
	try {
		std::ifstream ifs(file_name);
		if (ifs.is_open() == false) {
			printf("Cannot access file\n");
			return false;
		}
		ifs.exceptions(std::ifstream::badbit);

		json j = json::parse(ifs);

		for(auto & element: *parameters) {
			if (j.contains(element.name)) {
				if (element.type == file_parameter::T_FLOAT) {
					if (element.d_value)
						*element.d_value = j[element.name];
					else
						*element.od_value = j[element.name];
				}
				else if (element.type == file_parameter::T_INT) {
					if (element.i_value)
						*element.i_value = j[element.name];
					else
						*element.oi_value = j[element.name];
				}
				else if (element.type == file_parameter::T_BOOL)
					*element.b_value = j[element.name];
				else if (element.type == file_parameter::T_ABOOL) {
					*element.ab_value = j[element.name];
				}
			}
		}

		for(size_t group=0; group<pattern_groups; group++) {
			(*data)[group].dim   = j["patterns"][group]["dim"];
			auto & group_data    = (*data)[group];

			size_t index_note_delta = 0;
			for(auto & element: j["patterns"][group]["note-delta"])
				group_data.note_delta[index_note_delta++]          = element;

			size_t index_pattern    = 0;
			for(auto & element: j["patterns"][group]["pattern"])
				group_data.pattern   [index_pattern++   ].selected = element;

			if (j["patterns"][group].contains("volume-left")) {
				size_t index_volume_left = 0;
				for(auto & element: j["patterns"][group]["volume-left"])
					group_data.volume_left[index_volume_left++]   = element;
				size_t index_volume_right = 0;
				for(auto & element: j["patterns"][group]["volume-right"])
					group_data.volume_right[index_volume_right++] = element;
			}
			else {
				for(size_t i=0; i<index_note_delta; i++) {
					group_data.volume_left[i]                  = 1.;
					group_data.volume_right[i]                 = 1.;
				}
			}

			if (index_pattern < (*data)[group].dim || index_note_delta != index_pattern) {
				printf("note-delta count (%zu) or pattern count (%zu) not %zu\n", index_note_delta, index_pattern, (*data)[group].dim);
				return false;
			}
		}

		for(size_t group=0; group<pattern_groups; group++) {
			sample & s = (*sample_files)[group];
			s.name = j["samples"][group]["file-name"];
			delete s.s;
			s.s = nullptr;

			if (j.contains("midi-notes")) {
				int note = j["midi-notes"][group];
				if (note != -1)
					s.midi_note = note;
			}

			if (s.name.empty() == false) {
				printf("Loading \"%s\"...\n", s.name.c_str());
				const std::vector<std::vector<double> > sample_data = j["samples"][group]["data"];
				s.s = new sound_sample(sample_rate, s.name, sample_data, j["samples"][group]["sample-rate"]);
				if (s.s->begin() == false) {
					delete s.s;
					s.s = nullptr;
					s.name.clear();
					printf("Cannot init sample %s\n", s.name.c_str());
				}
				if (!s.s)
					return false;
				bool is_stereo = s.s->get_n_channels() >= 2;
				s.s->add_mapping(0, 0, j["samples"][group]["vol-left"]);
				if (is_stereo)
					s.s->add_mapping(1, 1, j["samples"][group]["vol-right"]);
				else
					s.s->add_mapping(0, 1, 1.0);  // mono -> right
				s.s->set_pitch_bend(j["samples"][group]["pitch"]);
				if (j["samples"][group].contains("mute"))
					s.s->set_mute(j["samples"][group]["mute"]);
				else
					s.s->set_mute(false);
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
