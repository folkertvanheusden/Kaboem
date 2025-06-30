#include <alsa/asoundlib.h>


void send_note(snd_seq_t *const seq, const int out_port, const int note, const int velocity);
std::pair<snd_seq_t *, int> allocate_midi_output_port();
std::pair<snd_seq_t *, int> allocate_midi_input_port();
