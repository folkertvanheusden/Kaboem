#pragma once
#include <map>
#include <string>

#include <libremidi/libremidi.hpp>


struct midi_handle_wrapper_in {
	libremidi::midi_in    *in;
};

struct midi_handle_wrapper_out {
	libremidi::midi_out   *out;
};

struct midi_in_pair {
	libremidi::API         api;
	libremidi::input_port  port;
};

struct midi_out_pair {
	libremidi::API         api;
	libremidi::output_port port;
};

bool init_midi  ();
void deinit_midi();

midi_handle_wrapper_out allocate_midi_output_port();
midi_handle_wrapper_in  allocate_midi_input_port ();
void                       send_midi_note           (midi_handle_wrapper_out & out_port, const int channel, const int note, const int velocity);
void                       send_pitch_bend          (midi_handle_wrapper_out & out_port, const int channel, const uint16_t pb);
void                       send_generic_midi_message(midi_handle_wrapper_out & p, const std::vector<uint8_t> & msg);
std::vector<unsigned char> receive_midi_note(midi_handle_wrapper_in  & in_port);
void close_midi_in_port (midi_handle_wrapper_in  & in_port);
void close_midi_out_port(midi_handle_wrapper_out & out_port);
