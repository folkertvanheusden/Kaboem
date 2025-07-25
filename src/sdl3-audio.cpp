#include <atomic>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <SDL3/SDL.h>

#include "gui.h"
#include "sdl3-audio.h"
#include "sound.h"
#include "time.h"


extern std::atomic_bool do_exit;

void on_process_audio(void *userdata, SDL_AudioStream *astream, int additional_amount, int total_amount)
{
	if (additional_amount == 0)
		return;

	sound_parameters *sound_pars  = reinterpret_cast<sound_parameters *>(userdata);
	int               period_size = std::min(additional_amount, sound_pars->pw.frames);

	std::vector<float> data;

	int  sleep_n = 16;
	bool warning_shown = false;
	std::unique_lock<std::shared_mutex> stream_lck(sound_pars->stream_lock);
	while(!do_exit) {
		bool empty = sound_pars->stream.empty();
		if (!empty) {
			auto & cur = sound_pars->stream.front();
			data.insert(data.end(), cur.begin(), cur.end());
			sound_pars->stream.pop();
		}

		if (data.size() >= size_t(period_size * 4 * 2))
			break;

		if (empty) {
			stream_lck.unlock();
			if (warning_shown == false) {
				warning_shown = true;
				printf("underflow (got %zu of %d)\n", data.size(), period_size);
			}
			my_us_sleep(sleep_n);
			if (sleep_n < 1024)
				sleep_n *= 2;
			stream_lck.lock();
		}
	}
	stream_lck.unlock();

	if (do_exit)
		return;

	if (SDL_PutAudioStreamData(astream, data.data(), data.size() * sizeof(float)) == false)
		SDL_Log("Couldn't play audio stream: %s", SDL_GetError());

	// scope
	std::unique_lock<std::shared_mutex> lck(sound_pars->stats_lock);
	sound_pars->scope = std::move(data);
	sound_pars->scope_t++;

	// statistics
	if (sound_pars->n_loud_checked >= sound_pars->sample_rate / 2) {
		if (sound_pars->too_loud_count > 0)
			sound_pars->clip_factor = sound_pars->too_loud_total / sound_pars->too_loud_count;
		else
			sound_pars->clip_factor = 0;
		sound_pars->too_loud_total = 0;
		sound_pars->too_loud_count = 0;
		sound_pars->n_loud_checked = 0;
		if (sound_pars->n_busyness)
			sound_pars->busyness = sound_pars->t_busyness / sound_pars->n_busyness;
		sound_pars->n_busyness     = 0;
		sound_pars->t_busyness     = 0;
	}
}

bool configure_sdl3_audio(sound_parameters *const target)
{
	target->pw.spec.channels = target->n_channels;
	target->pw.spec.format   = SDL_AUDIO_F32;
	target->pw.spec.freq     = target->sample_rate;
	target->pw.stream        = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &target->pw.spec, on_process_audio, target);
	if (!target->pw.stream) {
		SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
		return false;
	}

	SDL_AudioDeviceID device_id     = SDL_GetAudioStreamDevice(target->pw.stream);
	SDL_AudioSpec     ignored        { };
	if (SDL_GetAudioDeviceFormat(device_id, &ignored, &target->pw.frames))
		printf("sample_frames: %d\n", target->pw.frames);

	// start stream
	if (SDL_ResumeAudioStreamDevice(target->pw.stream) == false)
		SDL_Log("Couldn't start audio stream: %s", SDL_GetError());

	return true;
}

void stop_sdl3_audio(sound_parameters *const target)
{
        SDL_PauseAudioStreamDevice(target->pw.stream);
        SDL_DestroyAudioStream    (target->pw.stream);
}
