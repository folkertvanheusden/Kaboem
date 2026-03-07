#include "config.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unistd.h>

#include "clock.h"
#include "frequencies.h"
#include "gui.h"
#include "midi.h"
#include "sdl3-audio.h"
#include "time.h"


ssize_t determine_pattern_index(const uint64_t now, std::atomic_bool *const polyrythmic, const std::atomic_int *const sleep_us, const int t_adjustment, const ssize_t current_dim, const size_t max_steps)
{
	if (*polyrythmic)
		return (now + t_adjustment) / *sleep_us % current_dim;

	return size_t((now + t_adjustment) / double(*sleep_us) * current_dim / double(max_steps)) % current_dim;
}

int64_t us_to_next_pattern(const uint64_t now, std::atomic_bool *const polyrythmic, const std::atomic_int *const sleep_us, const int t_adjustment, const ssize_t current_dim, const size_t max_steps)
{
	uint64_t us_per_interval = 0;

	if (*polyrythmic)
		us_per_interval = *sleep_us;
	else
		us_per_interval = double(*sleep_us) * current_dim / double(max_steps);

	uint64_t prev_t = (now / us_per_interval) * us_per_interval;

	return us_per_interval - (now - t_adjustment - prev_t);
}

void queue_sample(sound_parameters *const sound_pars, const int note_delta, const double volume_left, const double volume_right,
		const sample *const s, pattern *const pat, const size_t pat_nr, midi_handle_wrapper_out midi_port)
{
	if (!s->s) {
		printf("Queuing sample without samples!\n");
		return;
	}

	pat->playing = true;

	sound_parameters::queued_sound qs { };
	qs.s     = s->s;
	qs.t     = 0;

	int    base_note       = qs.s->get_base_midi_note();
	double base_note_f     = midi_note_to_frequency(base_note);
	int    adjusted_note   = base_note + note_delta;
	double adjusted_note_f = midi_note_to_frequency(adjusted_note);

	double pitch           = base_note_f ? adjusted_note_f / base_note_f : 1.;
	qs.pat                 = pat;
	qs.pitch               = pitch;
	qs.volume_left         = volume_left;
	qs.volume_right        = volume_right;
	qs.echo_t              = s->echo_t;
	qs.history.reserve(s->s->get_sample_count() + s->echo_t);
	qs.pat_nr              = pat_nr;

	if (pat) {
		float lp_cutoff = pat->lp_cutoff.has_value() ? pat->lp_cutoff.value() : 0;
		float hp_cutoff = pat->hp_cutoff.has_value() ? pat->hp_cutoff.value() : sample_rate / 2;
		if (pat->lp_cutoff.has_value() || pat->hp_cutoff.has_value())
			qs.bp_filter = design_bandpass(sample_rate, lp_cutoff, hp_cutoff);
	}

	std::lock_guard <std::shared_mutex> lck(sound_pars->sounds_lock);
	bool hit = false;
	if (pat->serial_notes) {
		for(auto & sound: sound_pars->sounds) {
			if (sound.pat != pat)
				continue;

			if (sound.s->can_repeat())
				sound.end_requested = true;
			else {
				delete sound.bp_filter;
				sound = qs;
				hit = true;
				break;
			}
		}
	}
	if (!hit)
		sound_pars->sounds.push_back(qs);

	// TODO move this to sdl3-audio code? or the mixer?
	if (s->midi_note.has_value()) {
		uint16_t pb = 0x2000 * qs.pitch;

#if HAVE_SMF == 1
		{
			std::unique_lock<std::mutex> lck(sound_pars->smf_lock);
			if (sound_pars->smf_track) {
				double when = (get_us() - sound_pars->smf_start) / 1000000.;

				if (pb != 0x2000) {
					uint8_t pb_msg[3] = { 0xe9, uint8_t(pb & 127), uint8_t((pb >> 7) & 127) };
					smf_event_t *event = smf_event_new_from_pointer(pb_msg, sizeof pb_msg);
					smf_track_add_event_seconds(sound_pars->smf_track, event, when);
				}

				uint8_t msg[3] = { 0x99, uint8_t(s->midi_note.value()), 127 };
				smf_event_t *event = smf_event_new_from_pointer(msg, sizeof msg);
				smf_track_add_event_seconds(sound_pars->smf_track, event, when);
			}
		}
#endif

		if (midi_port.out) {
			if (pb != 0x2000)
				send_pitch_bend(midi_port, pb);
			send_midi_note(midi_port, s->midi_note.value(), 127);
		}
	}
}

void queue_sample(sound_parameters *const sound_pars, const ssize_t pat_index, const sample *const s, pattern *const pat, const size_t pat_nr, midi_handle_wrapper_out midi_port)
{
	queue_sample(sound_pars, pat->note_delta[pat_index], pat->volume_left[pat_index], pat->volume_right[pat_index], s, pat, pat_nr, midi_port);
}

void player(std::array<pattern, pattern_groups> *const pat_clickables, std::shared_mutex *const pat_clickables_lock,
		const std::array<sample, pattern_groups> *const samples,
		std::atomic_int  *const sleep_us, sound_parameters *const sound_pars,
		std::atomic_bool *const pause,    std::atomic_bool *const do_exit,
		std::atomic_bool *const force_trigger,
		std::atomic_bool *const polyrythmic,
		std::atomic_int  *const humanize_factor)
{
	auto                                midi_port      = allocate_midi_output_port();
	std::array<ssize_t, pattern_groups> prev_pat_index1;
	std::array<ssize_t, pattern_groups> prev_pat_index2;

	for(size_t i=0; i<pattern_groups; i++) {
		prev_pat_index1[i] = size_t(-1);
		prev_pat_index2[i] = size_t(-1);
	}

	while(!*do_exit) {
		uint64_t start = get_us();

		if (*pause) {
			my_us_sleep(10000);
			continue;
		}

		{
			std::shared_lock<std::shared_mutex> pat_lck(*pat_clickables_lock);

			uint64_t now       = my_clock;
			size_t   max_steps = 0;

			if (!*polyrythmic) {
				for(size_t i=0; i<pattern_groups; i++) {
					if ((*samples)[i].s != nullptr)
						max_steps = std::max(max_steps, (*pat_clickables)[i].dim);
				}
			}

			for(size_t i=0; i<pattern_groups; i++) {
				ssize_t current_dim = (*pat_clickables)[i].dim;
				int     swing       = (*pat_clickables)[i].swing * 1000;
				int     delay       = (*pat_clickables)[i].delay * 1000;

				int     humanize    = 0;
				int     sw_fac      = *humanize_factor;
				if (sw_fac) {
					sw_fac  *= 1000;  // microseconds
					humanize = (rand() % sw_fac) - sw_fac / 2;
				}

				ssize_t real_pat_index = determine_pattern_index(now, polyrythmic, sleep_us, humanize + delay, current_dim, max_steps);
				int     current_swing  = real_pat_index & 1 ? swing: 0;
				ssize_t pat_index      = determine_pattern_index(now, polyrythmic, sleep_us,
						humanize + delay + current_swing, current_dim, max_steps);

				if ((pat_index != prev_pat_index1[i] && pat_index != prev_pat_index2[i]) || force_trigger->exchange(false)) {
					prev_pat_index2[i] = prev_pat_index1[i];
					prev_pat_index1[i] = pat_index;

					if ((*pat_clickables)[i].pattern[pat_index].selected)
						queue_sample(sound_pars, pat_index, &(*samples)[i], &(*pat_clickables)[i], i, midi_port);
				}
			}
		}

		int64_t to_sleep = 1000 - (get_us() - start);
		if (to_sleep > 0)
			my_us_sleep(to_sleep);
		else
			printf("slow system (player): %zd μs\n", ssize_t(to_sleep));
	}

	close_midi_out_port(midi_port);
}
