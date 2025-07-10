#include <atomic>
#include <shared_mutex>
#include <sndfile.h>
#include <SDL3/SDL.h>

#include "sound.h"


void mixer(std::atomic_bool *const do_exit, sound_parameters *const sound_pars)
{
	const int period_size = sound_pars->pw.frames;

	printf("Mixer thread started, period size: %d\n", period_size);

	while(*do_exit == false) {
		{
			std::unique_lock<std::shared_mutex> lck(sound_pars->stream_lock);
			if (sound_pars->stream.size() >= 2) {
				lck.unlock();
				SDL_Delay(1);
				continue;
			}
		}

		printf("hier001 %d\n", period_size);

		float    *temp_buffer   = new float[sound_pars->n_channels * period_size]();
		float    *dest          = new float[sound_pars->n_channels * period_size]();

		for(int t=0; t<period_size; t++) {
			float *current_sample_base = &temp_buffer[t * sound_pars->n_channels];

			for(size_t s_idx=0; s_idx<sound_pars->sounds.size();) {
				auto & item = sound_pars->sounds[s_idx];
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
					sound_pars->sounds.erase(sound_pars->sounds.begin() + s_idx);
				else {
					item.t++;
					s_idx++;
				}
			}
		}

		printf("hier002\n");

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

		printf("hier003\n");

		{
			std::shared_lock<std::shared_mutex> lck(sound_pars->sounds_lock);
			if (sound_pars->record_handle)
				sf_writef_float(sound_pars->record_handle, dest, period_size);
		}

		printf("PUSH data\n");

		std::vector<float> samples(dest, dest + sound_pars->n_channels * period_size * sizeof(float));
		std::unique_lock<std::shared_mutex> lck(sound_pars->stream_lock);
		sound_pars->stream.push(samples);

		printf("hier004\n");

		delete [] dest;

		printf("hier005\n");
	}

	printf("Mixer thread terminating\n");
}
