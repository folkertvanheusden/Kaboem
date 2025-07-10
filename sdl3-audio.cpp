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
	uint64_t t_start = get_us();

	if (additional_amount == 0)
		return;

	sound_parameters *sp = reinterpret_cast<sound_parameters *>(userdata);
	int    period_size   = std::min(additional_amount, sp->pw.frames);
	double latency       = period_size * 1000000.0 / sp->sample_rate;

	std::vector<float> data;

	while(!do_exit) {
		bool empty = true;

		{
			std::unique_lock<std::shared_mutex> lck(sp->stream_lock);
			empty = sp->stream.empty();
			if (!empty) {
				data = sp->stream.front();
				sp->stream.pop();
			}
		}

		if (!empty)
			break;

		printf("underflow\n");
		SDL_Delay(1);
	}
	if (do_exit)
		return;

	if (SDL_PutAudioStreamData(astream, data.data(), data.size()) == false)
		SDL_Log("Couldn't play audio stream: %s", SDL_GetError());

	// scope
	sp->scope.clear();
	sp->scope.resize(period_size);

	for(int i=0; i<period_size; i++) {
		for(int c=0; c<sp->n_channels; c++)
			sp->scope[i] += data[i * sp->n_channels + c];

		sp->scope[i] /= sp->n_channels;
	}

	sp->scope_t++;

	// statistics
	sp->n_busyness++;
	sp->t_busyness += 100 * (get_us() - t_start) / latency;

	if (sp->n_loud_checked >= sp->sample_rate / 2) {
		if (sp->too_loud_count > 0)
			sp->clip_factor = sp->too_loud_total / sp->too_loud_count;
		else
			sp->clip_factor = 0;
		sp->too_loud_total = 0;
		sp->too_loud_count = 0;
		sp->n_loud_checked = 0;
		sp->busyness       = sp->t_busyness / sp->n_busyness;
		sp->n_busyness     = 0;
		sp->t_busyness     = 0;
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
