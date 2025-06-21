#pragma once

#include <shared_mutex>
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

	int                  sample_rate     { 0 };
	int                  n_channels      { 0 };

	pipewire_data_audio  pw;

	std::shared_mutex    sounds_lock;
	std::vector<std::pair<sound *, double> > sounds;

	double               global_volume;
};

void configure_pipewire_audio(sound_parameters *const pw);
