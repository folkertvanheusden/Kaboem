#pragma once

#include <shared_mutex>
#include <sndfile.h>
#include <thread>
#include <vector>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include "sound.h"


class pipewire_data_audio
{
public:
	std::thread       *th            { nullptr };

        pw_main_loop      *loop          { nullptr };
        pw_stream         *stream        { nullptr };
	spa_pod_builder    b;
        const spa_pod     *params[1]     { nullptr };
        uint8_t            buffer[1024]  { 0       };
	spa_audio_info_raw saiw          { SPA_AUDIO_FORMAT_UNKNOWN };
	pw_stream_events   stream_events { 0       };
};

class sound_parameters
{
public:
	sound_parameters(const int sample_rate, const int n_channels) :
       		sample_rate(sample_rate),
		n_channels(n_channels) {
	}

	virtual ~sound_parameters() {
	}

	int                  sample_rate     { 0       };
	int                  n_channels      { 0       };

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
};

void configure_pipewire_audio(sound_parameters *const pw);
