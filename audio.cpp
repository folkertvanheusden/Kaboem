#include <cfloat>
#include <cmath>
#include <mutex>
#include <SDL3/SDL.h>

#include "audio.h"
#include "gui.h"
#include "sound.h"
#include "time.h"


int counter = 0;
uint64_t sub_ts = 0;

void on_process_audio(void *userdata, SDL_AudioStream *astream, int additional_amount, int total_amount)
{
	if (additional_amount == 0)
		return;
	uint64_t          t_start  = get_us();

	sound_parameters *sp = reinterpret_cast<sound_parameters *>(userdata);

	static uint64_t pt_start = 0;
	if (pt_start)
		printf("%lu on_process_audio @ %lu - %d / %d\n", t_start - sub_ts, t_start - pt_start, additional_amount, total_amount);
	pt_start = t_start;
	counter++;

	int    stride       = sizeof(float) * sp->n_channels;
	int    period_size  = std::min(additional_amount, sp->pw.frames);
	double latency      = period_size * 1000000.0 / sp->sample_rate;

	float *temp_buffer   = new float[sp->n_channels * period_size]();
	float *dest          = new float[sp->n_channels * period_size]();

	std::shared_lock<std::shared_mutex> lck(sp->sounds_lock);

	for(int t=0; t<period_size; t++) {
		float *current_sample_base = &temp_buffer[t * sp->n_channels];

		for(size_t s_idx=0; s_idx<sp->sounds.size();) {
			auto & item = sp->sounds[s_idx];
			if (item.s == nullptr) {
				s_idx++;
				continue;
			}

			bool   fin               = false;
			size_t n_source_channels = item.s->get_n_channels();

			for(size_t ch=0; ch<n_source_channels; ch++) {
				auto rc = item.s->get_sample(item.t * item.pitch, ch);

				if (rc.has_value() == false)
					fin = true;
				else if (item.s->get_mute() == false) {
					float value = rc.value().first * (ch ? item.volume_right : item.volume_left);

					for(auto mapping : rc.value().second)
						current_sample_base[mapping.first] += value * mapping.second;
				}
			}

			if (fin)
				sp->sounds.erase(sp->sounds.begin() + s_idx);
			else {
				item.t++;
				s_idx++;
			}
		}
	}

	sp->n_loud_checked += period_size;

	if (sp->agc_enabled) {
		float *c_temp = new float[sp->n_channels];
		for(int t=0; t<period_size; t++) {
			float *current_sample_base_in  = &temp_buffer[t * sp->n_channels];
			float *current_sample_base_out = &dest[t * sp->n_channels];

			float gain = DBL_MAX;
			for(int c=0; c<sp->n_channels; c++) {
				c_temp[c] = current_sample_base_in[c] * sp->global_volume;
				gain      = std::min(gain, float(sp->agc_instances[c]->calculate_gain(c_temp[c])));
			}

			for(int c=0; c<sp->n_channels; c++) {
				float temp = std::clamp(c_temp[c] * gain, -1.f, 1.f);

				if (sp->filter_lp)
					temp = sp->filter_lp->apply(temp);
				if (sp->filter_hp)
					temp = sp->filter_hp->apply(temp);

				float sign = temp < 0 ? -1 : 1;
				current_sample_base_out[c] = powf(fabsf(temp), sp->sound_saturation) * sign;
			}
		}
		delete [] c_temp;
	}
	else {
		for(int t=0; t<period_size; t++) {
			float *current_sample_base_in  = &temp_buffer[t * sp->n_channels];
			float *current_sample_base_out = &dest[t * sp->n_channels];

			float too_loud = 0;
			for(int c=0; c<sp->n_channels; c++) {
				float temp = current_sample_base_in[c] * sp->global_volume;

				if (temp < -1.)
					temp = -1., too_loud = std::max(too_loud, fabsf(temp));
				else if (temp > 1.)
					temp = 1.,  too_loud = std::max(too_loud, temp);

				if (sp->filter_lp)
					temp = sp->filter_lp->apply(temp);
				if (sp->filter_hp)
					temp = sp->filter_hp->apply(temp);

				float sign = temp < 0 ? -1 : 1;
				current_sample_base_out[c] = powf(fabsf(temp), sp->sound_saturation) * sign;
			}

			sp->too_loud_total += too_loud;
			sp->too_loud_count++;
		}
	}

	delete [] temp_buffer;

	if (SDL_PutAudioStreamData(astream, dest, period_size * stride) == false)
		SDL_Log("Couldn't play audio stream: %s", SDL_GetError());

	if (sp->record_handle) 
		sf_writef_float(sp->record_handle, dest, period_size);

	// scope
	sp->scope.clear();
	sp->scope.resize(period_size);

	for(int i=0; i<period_size; i++) {
		for(int c=0; c<sp->n_channels; c++)
			sp->scope[i] += dest[i * sp->n_channels + c];

		sp->scope[i] /= sp->n_channels;
	}

	delete [] dest;

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
