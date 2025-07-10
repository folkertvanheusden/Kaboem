#include <atomic>
#include <shared_mutex>
#include <sndfile.h>
#include <SDL3/SDL.h>

#include "sound.h"
#include "time.h"


void mixer(std::atomic_bool *const do_exit, sound_parameters *const sound_pars)
{
	const int period_size = sound_pars->pw.frames;

	printf("Mixer thread started, period size: %d\n", period_size);

	uint64_t t_offset = 0;
	uint64_t sr_sleep = 1000000 * period_size / sound_pars->sample_rate;

	while(*do_exit == false) {
		uint64_t start = get_us();
		std::shared_lock<std::shared_mutex> lck(sound_pars->sounds_lock);

		float    *temp_buffer   = new float[sound_pars->n_channels * period_size]();
		float    *dest          = new float[sound_pars->n_channels * period_size]();

		for(int t=0; t<period_size; t++) {
			uint64_t  now                 = get_us();
			float    *current_sample_base = &temp_buffer[t * sound_pars->n_channels];

			for(size_t s_idx=0; s_idx<sound_pars->sounds.size();) {
				auto & item = sound_pars->sounds[s_idx];
				if (item.s == nullptr) {
					s_idx++;
					continue;
				}

				if (t_offset == 0) {
					t_offset      = now - item.play_at;
				}
				else {
					uint64_t when = item.play_at + t_offset;
					if (when > now) {
//						printf("skip %zu, %zu\n", s_idx, when - now);
						s_idx++;
						continue;
					}
//					printf("play %zu, %zu\n", s_idx, now - when);

					if (item.playing == false) {
						auto td = now - when;
						if (td > 170000 || td < 165000)
							printf("%lu GET: %zu from %d\n", get_us(), td, item.nr);
					}
					item.playing = true;
				}

				bool   fin               = false;
				size_t n_source_channels = item.s->get_n_channels();
				double t_use             = item.t * item.pitch;

				for(size_t ch=0; ch<n_source_channels; ch++) {
					auto rc = item.s->get_sample(t_use, ch);

					if (rc.has_value() == false)
						fin = true;
					else if (item.s->get_mute() == false) {
						float value = rc.value().first * (ch ? item.volume_right : item.volume_left);

						for(auto mapping : rc.value().second)
							current_sample_base[mapping.first] += value * mapping.second;
					}
				}

				if (fin)
					sound_pars->sounds.erase(sound_pars->sounds.begin() + s_idx);
				else {
					item.t++;
					s_idx++;
				}
			}
		}

		sound_pars->n_loud_checked += period_size;

		if (sound_pars->agc_enabled) {
			float *c_temp = new float[sound_pars->n_channels];
			for(int t=0; t<period_size; t++) {
				float *current_sample_base_in  = &temp_buffer[t * sound_pars->n_channels];
				float *current_sample_base_out = &dest       [t * sound_pars->n_channels];

				float gain = DBL_MAX;
				for(int c=0; c<sound_pars->n_channels; c++) {
					c_temp[c] = current_sample_base_in[c] * sound_pars->global_volume;
					gain      = std::min(gain, float(sound_pars->agc_instances[c]->calculate_gain(c_temp[c])));
				}

				for(int c=0; c<sound_pars->n_channels; c++) {
					float temp = std::clamp(c_temp[c] * gain, -1.f, 1.f);

					if (sound_pars->filter_lp)
						temp = sound_pars->filter_lp->apply(temp);
					if (sound_pars->filter_hp)
						temp = sound_pars->filter_hp->apply(temp);

					float sign = temp < 0 ? -1 : 1;
					current_sample_base_out[c] = powf(fabsf(temp), sound_pars->sound_saturation) * sign;
				}
			}
			delete [] c_temp;
		}
		else {
			for(int t=0; t<period_size; t++) {
				float *current_sample_base_in  = &temp_buffer[t * sound_pars->n_channels];
				float *current_sample_base_out = &dest[t * sound_pars->n_channels];

				float too_loud = 0;
				for(int c=0; c<sound_pars->n_channels; c++) {
					float temp = current_sample_base_in[c] * sound_pars->global_volume;

					if (temp < -1.)
						temp = -1., too_loud = std::max(too_loud, fabsf(temp));
					else if (temp > 1.)
						temp = 1.,  too_loud = std::max(too_loud, temp);

					if (sound_pars->filter_lp)
						temp = sound_pars->filter_lp->apply(temp);
					if (sound_pars->filter_hp)
						temp = sound_pars->filter_hp->apply(temp);

					float sign = temp < 0 ? -1 : 1;
					current_sample_base_out[c] = powf(fabsf(temp), sound_pars->sound_saturation) * sign;
				}

				sound_pars->too_loud_total += too_loud;
				sound_pars->too_loud_count++;
			}
		}

		delete [] temp_buffer;

		if (sound_pars->record_handle)
			sf_writef_float(sound_pars->record_handle, dest, period_size);

		std::vector<float> samples(dest, dest + sound_pars->n_channels * period_size * sizeof(float));
		lck.unlock();

		// queue for sdl3-audio
		std::shared_lock<std::shared_mutex> s_lck(sound_pars->stream_lock);
		sound_pars->stream.push(samples);

		delete [] dest;

		uint64_t end     = get_us();
		uint64_t took    = end - start;
		int64_t  sleep_n = sr_sleep - took;
		// printf("%zu %zu %zd\n", sr_sleep, took, sleep_n);
		if (sleep_n > 0)
			usleep(sleep_n);
	}

	printf("Mixer thread terminating\n");
}
