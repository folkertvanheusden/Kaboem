#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <alsa/asoundlib.h>

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
	if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) == -1) {
		fprintf(stderr, "Error opening ALSA sequencer\n");
		return { nullptr, -1 };
	}

	snd_seq_set_client_name(seq, "Kaboem");

	int out_port = snd_seq_create_simple_port(seq, "output",
			SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
			SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION);

	if (out_port == -1) {
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

void player(const std::array<std::vector<clickable>, pattern_groups> *const pat_clickables, std::shared_mutex *const pat_clickables_lock,
		const std::array<sample, pattern_groups> *const samples,
		std::atomic_int *const sleep_ms, sound_parameters *const sound_pars,
		std::atomic_bool *const pause, std::atomic_bool *const do_exit)
{
	auto      midi_port      = allocate_midi_output_port();

	size_t    prev_pat_index = size_t(-1);
	const int steps          = 16;

	while(!*do_exit) {
		if (*pause) {
			usleep(10000);
			continue;
		}

		{
			std::shared_lock<std::shared_mutex> pat_lck(*pat_clickables_lock);
			size_t pat_index = get_ms() / *sleep_ms % steps;
			if (pat_index != prev_pat_index) {
				std::unique_lock<std::shared_mutex> lck(sound_pars->sounds_lock);
				for(size_t i=0; i<pattern_groups; i++) {
					if ((*pat_clickables)[i][pat_index].selected) {
						if ((*samples)[i].s)
							sound_pars->sounds.push_back({ (*samples)[i].s, 0 });
						if ((*samples)[i].midi_note.has_value())
							send_note(midi_port.first, midi_port.second, (*samples)[i].midi_note.value(), 127);
					}
				}
				printf("%zu\n", sound_pars->sounds.size());
				lck.unlock();
				prev_pat_index = pat_index;
			}
		}

		// TODO depending on next sample timestamp
		usleep(1000);
	}

	snd_seq_close(midi_port.first);
}
