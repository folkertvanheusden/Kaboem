#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

#include "frequencies.h"
#include "gui.h"
#include "midi.h"
#include "sdl3-audio.h"
#include "time.h"


void player(const std::array<pattern, pattern_groups> *const pat_clickables, std::shared_mutex *const pat_clickables_lock,
		const std::array<sample, pattern_groups> *const samples,
		std::atomic_int  *const sleep_us, sound_parameters *const sound_pars,
		std::atomic_bool *const pause,    std::atomic_bool *const do_exit,
		std::atomic_bool *const force_trigger,
		std::atomic_bool *const polyrythmic,
		std::atomic_int  *const swing_factor,
		std::atomic_uint64_t *const t_start)
{
	auto                                midi_port      = allocate_midi_output_port();
	std::array<ssize_t, pattern_groups> prev_pat_index1;
	std::array<ssize_t, pattern_groups> prev_pat_index2;

	for(size_t i=0; i<pattern_groups; i++) {
		prev_pat_index1[i] = size_t(-1);
		prev_pat_index2[i] = size_t(-1);
	}

	std::array<int, pattern_groups> swing { };

	while(!*do_exit) {
		uint64_t start = get_us();

		if (*pause) {
			usleep(10000);
			continue;
		}

		{
			std::lock_guard <std::shared_mutex> lck    (sound_pars->sounds_lock);
			std::shared_lock<std::shared_mutex> pat_lck(*pat_clickables_lock);

			auto now = get_us() - *t_start;

			size_t max_steps = 0;
			if (!*polyrythmic) {
				for(size_t i=0; i<pattern_groups; i++) {
					if ((*samples)[i].s != nullptr)
						max_steps = std::max(max_steps, (*pat_clickables)[i].dim);
				}
			}

			for(size_t i=0; i<pattern_groups; i++) {
				ssize_t pat_index   = 0;
				ssize_t current_dim = (*pat_clickables)[i].dim;

				int sw_fac = *swing_factor;
				if (sw_fac)
					swing[i] = (rand() % sw_fac) - sw_fac / 2;
				else
					swing[i] = 0;

				if (*polyrythmic)
					pat_index = (now - swing[i]) / *sleep_us % current_dim;
				else
					pat_index = size_t((now - swing[i]) / double(*sleep_us) * current_dim / double(max_steps)) % current_dim;

				if ((pat_index != prev_pat_index1[i] && pat_index != prev_pat_index2[i]) || force_trigger->exchange(false)) {
					prev_pat_index2[i] = prev_pat_index1[i];
					prev_pat_index1[i] = pat_index;

					if ((*pat_clickables)[i].pattern[pat_index].selected) {
						if ((*samples)[i].s) {
							sound_parameters::queued_sound qs { };
							qs.s     = (*samples)[i].s;
							qs.t     = 0;

							int    base_note       = qs.s->get_base_midi_note();
							double base_note_f     = midi_note_to_frequency(base_note);
							int    adjusted_note   = base_note + (*pat_clickables)[i].note_delta[pat_index];
							int    adjusted_note_f = midi_note_to_frequency(adjusted_note);

							double pitch           = base_note_f ? adjusted_note_f / base_note_f : 1.;
							qs.pitch        = pitch;
							qs.volume_left  = (*pat_clickables)[i].volume_left [pat_index];
							qs.volume_right = (*pat_clickables)[i].volume_right[pat_index];

							extern int counter;
							static uint64_t p_push = 0;
							extern uint64_t sub_ts;
							uint64_t now = get_us();
							printf("%lu push %d after %lu\n", now, counter, now - p_push);
							sub_ts = now;
							p_push = now;
							counter = 0;
							sound_pars->sounds.push_back(qs);
						}

						if ((*samples)[i].midi_note.has_value()) {
							if (sound_pars->smf_track) {
								uint8_t msg[3] = { 0x99, (*samples)[i].midi_note.value(), 127 };
								smf_event_t *event = smf_event_new_from_pointer(msg, sizeof msg);
								smf_track_add_event_seconds(sound_pars->smf_track, event, (get_us() - sound_pars->smf_start) / 1000000.);
							}

							if (midi_port)
								send_midi_note(midi_port, (*samples)[i].midi_note.value(), 127);
						}
					}
				}
			}
		}

		int64_t to_sleep = 1000 - (get_us() - start);
		if (to_sleep > 0)
			usleep(to_sleep);
	}

	close_midi_out_port(midi_port);
}
