#include <utility>

#include "gui.h"
#include "midi.h"


bool init_midi()
{
	return true;
}

void deinit_midi()
{
}

midi_handle_wrapper_out allocate_midi_output_port()
{
	libremidi::midi_out *midi = new libremidi::midi_out;
	midi->open_virtual_port(PROG_NAME " OUTPUT");
	return { midi };
}

void send_midi_note(midi_handle_wrapper_out & p, const int note, const int velocity)
{
	if (p.out)
		p.out->send_message(0x99, note, velocity);
}

void send_pitch_bend(midi_handle_wrapper_out & p, const uint16_t pb)
{
	if (p.out)
		p.out->send_message(0xe9, pb & 127, (pb >> 7) & 127);
}

midi_handle_wrapper_in allocate_midi_input_port()
{
	libremidi::input_configuration cfg;
	libremidi::midi_in *midi = new libremidi::midi_in(cfg);
	midi->open_virtual_port(PROG_NAME " INPUT");
	return { midi };
}

std::vector<unsigned char> receive_midi_note(midi_handle_wrapper_in & p)
{
	std::vector<unsigned char> message;
	// TODO
	return message;
}

void close_midi_in_port(midi_handle_wrapper_in & midi_port)
{
	delete midi_port.in;
}

void close_midi_out_port(midi_handle_wrapper_out & midi_port)
{
	delete midi_port.out;
}
