#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <alsa/asoundlib.h>

#include "frequencies.h"
#include "gui.h"
#include "pipewire-audio.h"


uint64_t get_ms()
{
	timespec ts { };
	clock_gettime(CLOCK_REALTIME, &ts);
	return uint64_t(ts.tv_sec) * uint64_t(1000) + uint64_t(ts.tv_nsec / 1000000);
}

static std::pair<snd_seq_t *, int> allocate_midi_output_port()
{
	snd_seq_t *seq = nullptr;
	if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
		fprintf(stderr, "Error opening ALSA sequencer\n");
		return { nullptr, -1 };
	}

	snd_seq_set_client_name(seq, PROG_NAME);

	int out_port = snd_seq_create_simple_port(seq, "output",
			SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
			SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION);

	if (out_port < 0) {
		fprintf(stderr, "Error creating sequencer port\n");
		return { nullptr, -1 };
	}

	return { seq, out_port };
}

static void send_note(snd_seq_t *const seq, const int out_port, const int note, const int velocity)
{
        snd_seq_event_t ev { };
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, out_port);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_ev_set_noteon(&ev, 10 - 1, note, velocity);  // midi is 1 based
        snd_seq_event_output_direct(seq, &ev);
}

void player(const std::array<pattern, pattern_groups> *const pat_clickables, std::shared_mutex *const pat_clickables_lock,
		const std::array<sample, pattern_groups> *const samples,
		std::atomic_int  *const sleep_ms, sound_parameters *const sound_pars,
		std::atomic_bool *const pause,    std::atomic_bool *const do_exit,
		std::atomic_bool *const force_trigger,
		std::atomic_bool *const polyrythmic,
		std::atomic_int  *const swing_factor)
{
	auto                               midi_port      = allocate_midi_output_port();
	std::array<size_t, pattern_groups> prev_pat_index;

	for(size_t i=0; i<pattern_groups; i++)
		prev_pat_index[i] = size_t(-1);

	std::array<int, pattern_groups> swing { };

	while(!*do_exit) {
		if (*pause) {
			usleep(10000);
			continue;
		}

		{
			auto now = get_ms();
			std::shared_lock<std::shared_mutex> pat_lck(*pat_clickables_lock);
			size_t max_steps = 0;
			if (!*polyrythmic) {
				for(size_t i=0; i<pattern_groups; i++) {
					if ((*samples)[i].s != nullptr)
						max_steps = std::max(max_steps, (*pat_clickables)[i].dim);
				}
			}

			for(size_t i=0; i<pattern_groups; i++) {
				size_t pat_index   = 0;
				size_t current_dim = (*pat_clickables)[i].dim;

				if (prev_pat_index[i] == current_dim - 1) {
					int sw_fac = *swing_factor;
					if (sw_fac)
						swing[i] = (rand() % sw_fac) - sw_fac / 2;
					else
						swing[i] = 0;
				}

				if (*polyrythmic)
					pat_index = (now - swing[i]) / *sleep_ms % current_dim;
				else
					pat_index = size_t((now - swing[i]) / double(*sleep_ms) * current_dim / double(max_steps)) % current_dim;

				if (pat_index != prev_pat_index[i] || force_trigger->exchange(false)) {
					if (pat_index < prev_pat_index[i] && pat_index != 0 && prev_pat_index[i] != size_t(-1))
						continue;
					prev_pat_index[i] = pat_index;

					std::lock_guard<std::shared_mutex> lck(sound_pars->sounds_lock);
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
							qs.pitch = pitch;

							sound_pars->sounds.push_back(qs);
						}

						if ((*samples)[i].midi_note.has_value() && midi_port.first)
							send_note(midi_port.first, midi_port.second, (*samples)[i].midi_note.value(), 127);
					}
				}
			}
		}

		usleep(1000000 / *sleep_ms);
	}

	if (midi_port.first)
		snd_seq_close(midi_port.first);
}
