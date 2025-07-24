#include "config.h"
#pragma once

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <math.h>
#include <optional>
#include <queue>
#include <set>
#include <shared_mutex>
#if HAVE_SMF == 1
#include <smf.h>
#endif
#include <sndfile.h>
#include <string>
#include <vector>

#include "agc.h"
#include "bp-filter.h"
#include "sdl3-audio.h"


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
	double delta_t     { 0.    };

	double volume_at_end_start { 0. };

	bool   muted       { false };

	// input channel, { output channel, volume }
	std::vector<float> volumes;

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

	virtual size_t get_sample_count() const = 0;

	virtual void set_control(const int nr, const int value)
	{
		printf("set control %d to %d: ", nr, value);
		controls.at(nr).current_setting = value / controls.at(nr).divide_by + controls.at(nr).add;
		printf("%f\n", controls.at(nr).current_setting);
	}

	void add_mapping(const int to, const float volume)
	{
		set_volume(to, volume);
	}

	float get_mapping_target_volume(const int to)
	{
		return volumes[to];
	}

	void set_pitch_bend(const double pb)
	{
		pitchbend = pb;
	}

	double get_pitch_bend()
	{
		return pitchbend;
	}

	void set_volume(const size_t to, const double v)
	{
		if (to >= volumes.size())
			volumes.resize(to + 1);
		volumes[to] = v;
	}

	void set_volume(const double v)
	{
		for(auto & to: volumes)
			to = v;
	}

	float get_avg_volume()
	{
		float v = 0;

		for(auto & to: volumes)
			v += to;

		return v / volumes.size();
	}

	float get_volume(const int to) const
	{
		return volumes[to];
	}

	virtual size_t get_n_channels() const = 0;

	// sample, output-channels
	virtual std::optional<std::pair<float, float> > get_sample(const double t, const size_t channel_nr) const = 0;

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
	std::string                      file_name;
	std::vector<std::vector<float> > samples;
	unsigned                         sample_sample_rate { 0  };
	double                           base_frequency     { 0. };
	int                              base_midi_note     { 0  };
	std::string                      name;

public:
	sound_sample(const int sample_rate, const std::string & file_name);
	sound_sample(const int sample_rate, const std::string & file_name, const std::vector<std::vector<float> > & sample_data, const unsigned sample_sample_rate);
	virtual ~sound_sample() { }

	bool begin();

	size_t get_n_channels() const override { return samples.at(0).size(); }
	size_t get_sample_count() const override { return samples.size(); }
	const std::vector<std::vector<float> > & get_raw() const { return samples; }
	unsigned get_sample_rate() const { return sample_sample_rate; }

	std::optional<std::pair<float, float> > get_sample(const double t, const size_t channel_nr) const override;

	std::string get_name() const override;
	double      get_base_frequency() const override { return base_frequency; }
	int         get_base_midi_note() const override { return base_midi_note; }
};

struct sample
{
	sound_sample      *s;
	std::string        name;
	std::optional<int> midi_note;
	int                echo_t;
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

	int                  sample_rate     { 0       };  // assuming this doesn't change at run-time
	int                  n_channels      { 0       };  // assuming this doesn't change at run-time
	std::vector<agc *>   agc_instances;
	bool                 agc_enabled     { false   };

	sdl3_data_audio      pw;

	std::mutex           midi_sample_lock;
	sample               midi_sample     {         };
	std::string          midi_sample_name;

	std::shared_mutex               stream_lock;
	std::queue<std::vector<float> > stream;

	std::shared_mutex    sounds_lock;  ///
	struct queued_sound {
		const sound   *s            { nullptr };
		int            t;
		double         pitch;
		double         volume_left;
		double         volume_right;
		int            echo_t;
		biquad_filter *bp_filter    { nullptr };
		std::optional<size_t>            pattern_idx;
		std::vector<std::vector<float> > history;
	};
	std::vector<queued_sound> sounds;
	double               global_volume    { 1.      };
	double               sound_saturation { 1.      };
	///

	std::shared_mutex    stats_lock;  ///
	std::vector<float>   scope;
	int                  scope_t          { 0       };

	double               too_loud_total   { 0.      };
	int                  too_loud_count   { 0       };
	int                  n_loud_checked   { 0       };
	double               clip_factor      { 0       };

	int                  n_busyness       { 0       };
	int                  t_busyness       { 0       };
	int                  busyness         { 0       };
	///

#if HAVE_SMF == 1
	std::mutex           smf_lock;
	smf_t               *smf              { nullptr };
	smf_track_t         *smf_track        { nullptr };
	uint64_t             smf_start        { 0       };
	std::string          smf_file_name;
#endif

	std::mutex           record_lock;
	SNDFILE             *record_handle    { nullptr };

	uint64_t             record_wav_smf_since { 0   };
};
