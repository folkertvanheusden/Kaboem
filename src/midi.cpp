#include <mutex>
#include <queue>
#include <utility>
#include <vector>

#include "gui.h"
#include "midi.h"


std::mutex midi_messages_lock;
std::queue<std::vector<uint8_t> > midi_messages;

bool init_midi()
{
	return true;
}

void deinit_midi()
{
}

std::map<std::string, midi_in_pair> get_midi_ports(const bool input)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(100));  // work around for window 10

	std::map<std::string, midi_in_pair> out;

	for(auto & api : libremidi::available_apis()) {
		std::string_view    api_name = libremidi::get_api_display_name(api);
		libremidi::observer midi  { {.track_hardware = true, .track_virtual = true}, libremidi::observer_configuration_for(api)};
		if (input) {
			auto        ports    = midi.get_input_ports();
			for(auto & port: ports)
				out.insert({ std::string(api_name) + " - " + port.display_name, { api, port } });
		}
		else {  // output
			auto        ports    = midi.get_output_ports();
			for(auto & port: ports)
				out.insert({ std::string(api_name) + " - " + port.display_name, { api, port } });
		}
	}

	return out;
}

midi_handle_wrapper_out allocate_midi_output_port()
{
	auto out_conf = libremidi::midi1::out_default_configuration();
// TODO	libremidi::set_client_name(out_conf, PROG_NAME);
	auto *midi_out = new libremidi::midi_out { libremidi::output_configuration{}, out_conf };
	midi_out->open_virtual_port("output");
	return { midi_out };
}

void send_midi_note(midi_handle_wrapper_out & p, const int channel, const int note, const int velocity)
{
	if (p.out)
		p.out->send_message(0x90 + channel, note, velocity);
}

void send_pitch_bend(midi_handle_wrapper_out & p, const int channel, const uint16_t pb)
{
	if (p.out)
		p.out->send_message(0xe0 + channel, pb & 127, (pb >> 7) & 127);
}

void send_generic_midi_message(midi_handle_wrapper_out & p, const std::vector<uint8_t> & msg)
{
	p.out->send_message(msg);
}

midi_handle_wrapper_in allocate_midi_input_port()
{
	auto  in_conf = libremidi::midi1::in_default_configuration();
// TODO	libremidi::set_client_name(in_conf, PROG_NAME);
	auto *midi_in = new libremidi::midi_in {
		{ .on_message = [](const libremidi::message& message) {
		      std::vector<uint8_t> message_to_q;
		      for(auto & b: message)
			      message_to_q.push_back(b);
		      std::unique_lock<std::mutex> lck(midi_messages_lock);
		      midi_messages.push(message_to_q);
	      } }, in_conf
	};
	midi_in->open_virtual_port("input");
	return { midi_in };
}

std::vector<unsigned char> receive_midi_note(midi_handle_wrapper_in & p)
{
	std::vector<unsigned char>   message;
	std::unique_lock<std::mutex> lck(midi_messages_lock);
	if (midi_messages.empty() == false) {
		message = midi_messages.front();
		midi_messages.pop();
	}
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
