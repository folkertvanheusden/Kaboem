#include "config.h"
#include <atomic>
#include <cassert>
#include <cfloat>
#include <fcntl.h>
#include <mutex>
#include <shared_mutex>
#include <sndfile.h>
#include <unistd.h>
#include <SDL3/SDL.h>

#include "gui.h"
#include "midi-handler.h"
#include "sound.h"
#include "time.h"


std::pair<std::vector<float>, std::vector<float> > mix(sound_parameters *const sound_pars, const int period_size, std::atomic_bool *const end_notes)
{
	std::vector<float> mixed_buffer(sound_pars->n_channels        * period_size);
	std::vector<float> all_channels(sound_pars->record_n_channels * period_size);

	for(int t=0; t<period_size; t++) {
		size_t mixed_base = t * sound_pars->n_channels;

		// sum all samples
		for(size_t s_idx=0; s_idx<sound_pars->sounds.size();) {
			auto & item = sound_pars->sounds[s_idx];
			if (item.s == nullptr) {
				printf("Sample without samples\n");
				delete item.bp_filter;
				item.pat  ->playing = false;
				sound_pars->sounds.erase(sound_pars->sounds.begin() + s_idx);
				continue;
			}

			if (item.play_started == false) {
				item.play_started = true;

				// debug: show latency between queueing and playing
				static uint64_t pt = 0;
				uint64_t now = get_us();
				// printf("S: %zu | %zu %zu\n", item.queued_at, now - item.queued_at, now - pt);
				pt = now;
			}

			size_t cur_n_chan = item.s->get_n_channels();
			bool   fin        = false;
			bool   mute       = item.s->get_mute();
			double t_use      = item.t * item.pitch;
			bool   apply_echo = item.echo_t > 0;

			if (!mute) {
				size_t do_n_channels = std::max(size_t(sound_pars->n_channels), cur_n_chan);
				std::vector<float> applied_echo(do_n_channels);

				for(size_t ch=0; ch<do_n_channels; ch++) {
					auto rc = item.s->get_sample(t_use, ch);

					if (rc.has_value() == false) {
						if (!apply_echo) {
							fin = true;
							break;
						}

						rc = { 0, 1 };
					}

					float value = rc.value().first;

					if (item.bp_filter)
						value = item.bp_filter->process(value);

					if (apply_echo && item.t >= item.echo_t) {
						constexpr const float feedback = 0.5;  // TODO configurable?
						value += feedback * item.history[item.t - item.echo_t][ch];
					}

					applied_echo.push_back(value);

					value *= rc.value().second;
					if (ch == 0)
						value *= item.volume_left;
					else if (ch == 1)
						value *= item.volume_right;

					if (ch < size_t(sound_pars->n_channels))
						mixed_buffer[mixed_base + ch] += value;

					if (!all_channels.empty() && ch<cur_n_chan)
						all_channels[sound_pars->record_ch_offsets[item.pat_nr] + t * sound_pars->record_n_channels + ch] = value;
				}

				item.history.push_back(std::move(applied_echo));
			}

			if (fin) {
				delete item.bp_filter;
				item.pat->playing = false;
				sound_pars->sounds.erase(sound_pars->sounds.begin() + s_idx);
			}
			else {
				item.s->get_new_t(&item.t, item.end_requested || *end_notes);
				s_idx++;
			}
		}
	}

	return { mixed_buffer, all_channels };
}

void apply_agc(sound_parameters *const sound_pars, const float *const buffer, std::vector<float> & dest, const int period_size)
{
	float *c_temp = new float[sound_pars->n_channels];
	for(int t=0; t<period_size; t++) {
		const float *current_sample_base_in = &buffer[t * sound_pars->n_channels];
		size_t       dest_index             = t * sound_pars->n_channels;

		float gain = DBL_MAX;
		for(int c=0; c<sound_pars->n_channels; c++) {
			c_temp[c] = current_sample_base_in[c] * sound_pars->global_volume;
			gain      = std::min(gain, sound_pars->agc_instances[c]->calculate_gain(c_temp[c]));
		}

		for(int c=0; c<sound_pars->n_channels; c++) {
			float temp = std::clamp(c_temp[c] * gain, -1.f, 1.f);
			float sign = temp < 0 ? -1 : 1;
			dest[dest_index + c] = powf(fabsf(temp), sound_pars->sound_saturation) * sign;
		}
	}
	delete [] c_temp;
}

std::pair<double, int> clip_too_loud(const size_t n_channels, const float *const buffer, std::vector<float> & dest, const int period_size, const float global_volume, const float saturation)
{
	double too_loud_total = 0;
	int    too_loud_count = 0;
	for(int t=0; t<period_size; t++) {
		const float *current_sample_base_in = &buffer[t * n_channels];
		size_t       dest_index             = t * n_channels;
		float        too_loud               = 0;
		for(size_t c=0; c<n_channels; c++) {
			float temp = current_sample_base_in[c] * global_volume;

			if (temp < -1.)
				temp = -1., too_loud = std::max(too_loud, fabsf(temp));
			else if (temp > 1.)
				temp = 1.,  too_loud = std::max(too_loud, temp);

			float sign = temp < 0 ? -1 : 1;
			dest[dest_index + c] = powf(fabsf(temp), saturation) * sign;
		}

		too_loud_total += too_loud;
		too_loud_count++;
	}

	return { too_loud_total, too_loud_count };
}

void write_wav(sound_parameters *const sound_pars, const std::vector<float> & data, const size_t n_channels)
{
	std::unique_lock<std::mutex> r_lck(sound_pars->record_lock);
	if (sound_pars->record_handle)
		sf_writef_float(sound_pars->record_handle, data.data(), data.size() / n_channels);
}

void mixer(std::atomic_bool *const do_exit, sound_parameters *const sound_pars, std::atomic_bool *const paused)
{
	const int period_size = 128;
	double    latency     = period_size * 1000000.0 / sound_pars->sample_rate;
	uint64_t  sr_sleep    = latency;

	printf("Mixer thread started, period size: %d (of %d), sleep: %zu μs\n", period_size, sound_pars->pw.frames, size_t(sr_sleep));

	while(*do_exit == false) {
		uint64_t t_start = get_us();

		std::unique_lock<std::shared_mutex> lck(sound_pars->sounds_lock);
		auto   temp_buffer = mix(sound_pars, period_size, paused);
		size_t n_sounds    = sound_pars->sounds.size();

		sound_pars->n_loud_checked += period_size;

		std::vector<float> dest(sound_pars->n_channels * period_size);
		if (sound_pars->agc_enabled)
			apply_agc    (sound_pars, temp_buffer.first.data(), dest, period_size);
		else {
			auto clip_stats = clip_too_loud(sound_pars->n_channels, temp_buffer.first.data(), dest, period_size, sound_pars->global_volume, sound_pars->sound_saturation);
			sound_pars->too_loud_total += clip_stats.first;
			sound_pars->too_loud_count += clip_stats.second;
		}

		lck.unlock();

		if (sound_pars->record_multichannel == false)
			write_wav(sound_pars, dest, 2);

		// queue for sdl3-audio
		std::unique_lock<std::shared_mutex> s_lck(sound_pars->stream_lock);
		sound_pars->stream.push({ t_start, std::move(dest) });

		size_t   n_buffers = sound_pars->stream.size();
		s_lck.unlock();

		if (sound_pars->record_multichannel == true && temp_buffer.second.empty() == false) {
			std::vector<float> dest_mc(temp_buffer.second.size());
			clip_too_loud(sound_pars->record_n_channels, temp_buffer.second.data(), dest_mc, period_size, sound_pars->global_volume, sound_pars->sound_saturation);
			write_wav(sound_pars, dest_mc, sound_pars->record_n_channels);
		}

		uint64_t t_end     = get_us();
		uint64_t took      = t_end - t_start;
		int64_t  sleep_n   = sr_sleep - took;

		if (n_buffers >= size_t(sound_pars->pw.frames * 2 / period_size)) {
			if (sleep_n > 0)
				my_us_sleep(sleep_n);
		}
		else {
			if (sleep_n < 0 && n_buffers == 0)
				printf("slow system (mixer): %zd μs, took: %zu μs, sounds: %zu\n", ssize_t(sleep_n), size_t(took), n_sounds);
		}

		std::unique_lock<std::shared_mutex> r_lck(sound_pars->stats_lock);
		sound_pars->n_busyness++;
		sound_pars->t_busyness += 100 * took / latency;
	}

	printf("Mixer thread terminating\n");
}
