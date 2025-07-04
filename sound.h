#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <math.h>
#include <optional>
#include <set>
#include <shared_mutex>
#include <sndfile.h>
#include <string>
#include <vector>

#include "agc.h"
#include "filter.h"
#include "pipewire-audio.h"


double f_to_delta_t(const double frequency, const int sample_rate);

class sound_control
{
public:
	// name
	enum { cm_continuous_controller } cm_mode;
	uint8_t cm_index;  // midi index (for 0xb0: data-1)
	int index;  // internal index
	std::string name;
	// how to transform a value
	double divide_by;
	double add;
	// last transformed value
	double current_setting;
};

class sound
{
protected:
	int    sample_rate { 44100 };
	double frequency   { 100.  };

	double pitchbend   { 1.    };

	double t           { 0.    };
	double delta_t     { 0.    };

	double volume_at_end_start { 0. };

	bool   muted       { false };

	// input channel, { output channel, volume }
	std::vector<std::map<int, double> > input_output_matrix;

	std::vector<sound_control> controls;

public:
	sound(const int sample_rate, const double frequency) :
		sample_rate(sample_rate),
		frequency(frequency)
	{
	}

	virtual std::vector<sound_control> get_controls()
	{
		return controls;
	}

	virtual void set_control(const int nr, const int value)
	{
		printf("set control %d to %d: ", nr, value);
		controls.at(nr).current_setting = value / controls.at(nr).divide_by + controls.at(nr).add;
		printf("%f\n", controls.at(nr).current_setting);
	}

	void add_mapping(const int from, const int to, const double volume)
	{
		// note that 'from' is ignored here as this object has only 1 generator
		input_output_matrix[from].insert({ to, volume });
	}

	double get_mapping_target_volume(const int to)
	{
		for(auto & mapping: input_output_matrix) {
			auto it = mapping.find(to);
			if (it != mapping.end())
				return it->second;
		}

		return 0.1;
	}

	void remove_mapping(const int from, const int to)
	{
		input_output_matrix[from].erase(to);
	}

	void set_pitch_bend(const double pb)
	{
		pitchbend = pb;
	}

	double get_pitch_bend()
	{
		return pitchbend;
	}

	void set_volume(const int from, const int to, const double v)
	{
		input_output_matrix[from][to] = v;
	}

	void set_mapping_target_volume(const int to, const double volume)
	{
		for(auto & mapping: input_output_matrix) {
			auto it = mapping.find(to);
			if (it != mapping.end()) {
				it->second = volume;
				break;
			}
		}
	}

	void set_volume(const double v)
	{
		for(size_t from=0; from<input_output_matrix.size(); from++) {
			for(auto & to: input_output_matrix.at(from))
				to.second = v;
		}
	}

	double get_avg_volume()
	{
		double v = 0;
		int n = 0;

		for(size_t from=0; from<input_output_matrix.size(); from++) {
			for(auto & to: input_output_matrix.at(from)) {
				v += to.second;
				n++;
			}
		}

		return v / n;
	}

	double get_volume(const int from, const int to)
	{
		return input_output_matrix[from][to];
	}

	virtual size_t get_n_channels() = 0;

	// sample, output-channels
	virtual std::pair<double, std::map<int, double> > get_sample(const size_t channel_nr) = 0;

	virtual bool set_time(const uint64_t t_in)
	{
		t = t_in * (delta_t * pitchbend);
		return false;
	}

	virtual std::string get_name()           const = 0;
	virtual double      get_base_frequency() const = 0;
	virtual int         get_base_midi_note() const = 0;

	void set_mute(const bool m)
	{
		muted = m;
	}

	bool get_mute() const
	{
		return muted;
	}
};

class sound_sample : public sound
{
private:
	std::string                       file_name;
	std::vector<std::vector<double> > samples;
	unsigned                          sample_sample_rate { 0  };
	double                            base_frequency     { 0. };
	int                               base_midi_note     { 0  };
	std::string                       name;

public:
	sound_sample(const int sample_rate, const std::string & file_name);
	sound_sample(const int sample_rate, const std::string & file_name, const std::vector<std::vector<double> > & sample_data, const unsigned sample_sample_rate);
	virtual ~sound_sample() { }

	bool begin();

	size_t get_n_channels() override
	{
		return samples.at(0).size();
	}

	const std::vector<std::vector<double> > & get_raw() const { return samples; }
	unsigned get_sample_rate() const { return sample_sample_rate; }

	std::pair<double, std::map<int, double> > get_sample(const size_t channel_nr) override;

	std::string get_name() const override;
	double      get_base_frequency() const { return base_frequency; }
	int         get_base_midi_note() const { return base_midi_note; }

	bool set_time(const uint64_t t_in) override
	{
		sound::set_time(t_in);
		return t >= samples.size();
	}
};

class sound_parameters
{
public:
	sound_parameters(const int sample_rate, const int n_channels) :
       		sample_rate(sample_rate),
		n_channels(n_channels) {
		for(int i=0; i<n_channels; i++)
			agc_instances.push_back(new agc(-10.0, 4.0, 10.0, 100.0, sample_rate));
	}

	virtual ~sound_parameters() {
		for(auto & a: agc_instances)
			delete a;
	}

	int                  sample_rate     { 0       };
	int                  n_channels      { 0       };
	std::vector<agc *>   agc_instances;
	bool                 agc_enabled     { false   };

	pipewire_data_audio  pw;

	std::shared_mutex    sounds_lock;
	struct queued_sound {
		sound *s;
		double t;
		double pitch;
	};
	std::vector<queued_sound> sounds;
	SNDFILE             *record_handle    { nullptr };
	filter_butterworth  *filter_lp        { nullptr };
	filter_butterworth  *filter_hp        { nullptr };
	double               global_volume    { 1.      };
	double               sound_saturation { 1.      };

	std::vector<double>  scope;
	int                  scope_t          { 0       };

	double               too_loud_total   { 0.      };
	int                  too_loud_count   { 0       };
	int                  n_loud_checked   { 0       };
	double               clip_factor      { 0       };

	int                  n_busyness       { 0       };
	int                  t_busyness       { 0       };
	int                  busyness         { 0       };
};
