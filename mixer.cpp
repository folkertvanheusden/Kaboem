#include "config.h"
#include <atomic>
#include <cfloat>
#include <mutex>
#include <shared_mutex>
#include <sndfile.h>
#include <unistd.h>
#include <SDL3/SDL.h>

#include "sound.h"
#include "time.h"


void mixer(std::atomic_bool *const do_exit, sound_parameters *const sound_pars)
{
	const int period_size = 128;
	double    latency     = period_size * 1000000.0 / sound_pars->sample_rate;

	printf("Mixer thread started, period size: %d (of %d)\n", period_size, sound_pars->pw.frames);

	uint64_t  sr_sleep    = latency;
	printf("sleep: %zu\n", sr_sleep);

	while(*do_exit == false) {
		uint64_t t_start = get_us();

		std::shared_lock<std::shared_mutex> lck(sound_pars->sounds_lock);
		float   *temp_buffer = new float[sound_pars->n_channels * period_size]();

		for(int t=0; t<period_size; t++) {
			float *current_sample_base = &temp_buffer[t * sound_pars->n_channels];

			// sum all samples
			for(size_t s_idx=0; s_idx<sound_pars->sounds.size();) {
				auto & item = sound_pars->sounds[s_idx];
				if (item.s == nullptr) {
					s_idx++;
					continue;
				}

				bool   fin        = false;
				bool   mute       = item.s->get_mute();
				double t_use      = item.t * item.pitch;
				bool   apply_echo = item.echo_t > 0;

				if (!mute) {
					std::vector<float> applied_echo;

					// assume stereo (maybe in the future 2+1? or even 5+1?)
					for(size_t ch=0; ch<2; ch++) {
						auto rc = item.s->get_sample(t_use, ch);

						if (rc.has_value() == false) {
							if (!apply_echo) {
								fin = true;
								break;
							}

							rc = { 0, 1 };
						}

						float value         = rc.value().first * (ch ? item.volume_right : item.volume_left);
						float value_volumed = value * rc.value().second;

						if (apply_echo && item.t >= item.echo_t) {
							constexpr const float feedback = 0.5;  // TODO configurable?
							value_volumed += feedback * item.history[item.t - item.echo_t][ch];
						}

						applied_echo.push_back(value_volumed);

						current_sample_base[ch] += value_volumed;
					}

					item.history.push_back(applied_echo);
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

		std::vector<float> dest(sound_pars->n_channels * period_size);

		if (sound_pars->agc_enabled) {
			float *c_temp = new float[sound_pars->n_channels];
			for(int t=0; t<period_size; t++) {
				float *current_sample_base_in  = &temp_buffer[t * sound_pars->n_channels];
				size_t dest_index              = t * sound_pars->n_channels;

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
					dest[dest_index + c] = powf(fabsf(temp), sound_pars->sound_saturation) * sign;
				}
			}
			delete [] c_temp;
		}
		else {
			for(int t=0; t<period_size; t++) {
				float *current_sample_base_in = &temp_buffer[t * sound_pars->n_channels];
				size_t dest_index             = t * sound_pars->n_channels;

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
					dest[dest_index + c] = powf(fabsf(temp), sound_pars->sound_saturation) * sign;
				}

				sound_pars->too_loud_total += too_loud;
				sound_pars->too_loud_count++;
			}
		}

		delete [] temp_buffer;

		lck.unlock();

		{
			std::unique_lock<std::mutex> r_lck(sound_pars->record_lock);
			if (sound_pars->record_handle)
				sf_writef_float(sound_pars->record_handle, dest.data(), dest.size() / sound_pars->n_channels);
		}

		// queue for sdl3-audio
		std::unique_lock<std::shared_mutex> s_lck(sound_pars->stream_lock);
		sound_pars->stream.push(dest);

		sound_pars->n_busyness++;
		sound_pars->t_busyness += 100 * (get_us() - t_start) / latency;

		if (sound_pars->stream.size() >= size_t(sound_pars->pw.frames * 2 / period_size)) {
			s_lck.unlock();

			uint64_t end     = get_us();
			uint64_t took    = end - t_start;
			int64_t  sleep_n = sr_sleep - took;
			if (sleep_n > 0)
				my_us_sleep(sleep_n);
			else
				printf("slow system (mixer): %zd\n", ssize_t(sleep_n));
		}
	}

	printf("Mixer thread terminating\n");
}
