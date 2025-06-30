#include <utility>
#include <alsa/asoundlib.h>

#include "gui.h"


std::pair<snd_seq_t *, int> allocate_midi_output_port()
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

void send_note(snd_seq_t *const seq, const int out_port, const int note, const int velocity)
{
        snd_seq_event_t ev { };
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, out_port);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_ev_set_noteon(&ev, 10 - 1, note, velocity);  // midi is 1 based
        snd_seq_event_output_direct(seq, &ev);
}

std::pair<snd_seq_t *, int> allocate_midi_input_port()
{
        snd_seq_t *seq = nullptr;
        if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
                fprintf(stderr, "Error opening ALSA sequencer\n");
                return { nullptr, -1 };
        }

        snd_seq_set_client_name(seq, PROG_NAME);

        int in_port = snd_seq_create_simple_port(seq, "input",
                        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
                        SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION);

        if (in_port < 0) {
                fprintf(stderr, "Error creating sequencer port\n");
                return { nullptr, -1 };
        }

        return { seq, in_port };
}

