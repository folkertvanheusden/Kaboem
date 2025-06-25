#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <math.h>
#include <optional>
#include <set>
#include <string>
#include <vector>


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

	virtual std::string get_name() const = 0;
};

class sound_sample : public sound
{
private:
	std::string                       file_name;
	std::vector<std::vector<double> > samples;

public:
	sound_sample(const int sample_rate, const std::string & file_name);
	virtual ~sound_sample() { }

	bool begin();

	size_t get_n_channels() override
	{
		return samples.at(0).size();
	}

	std::pair<double, std::map<int, double> > get_sample(const size_t channel_nr) override;

	std::string get_name() const override
	{
		return "sample";
	}

	bool set_time(const uint64_t t_in) override
	{
		sound::set_time(t_in);
		return t >= samples.size();
	}
};
