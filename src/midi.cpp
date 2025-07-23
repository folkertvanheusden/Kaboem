#include "config.h"
#if HAVE_RTMIDI == 1
#include <rtmidi/RtMidi.h>
#endif
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

RtMidiOut * allocate_midi_output_port()
{
#if HAVE_RTMIDI == 1
	auto *p = new RtMidiOut(RtMidi::Api::UNSPECIFIED, PROG_NAME);
	p->openVirtualPort("output");
	return p;
#else
	return nullptr;
#endif
}

void send_midi_note(RtMidiOut *const p, const int note, const int velocity)
{
#if HAVE_RTMIDI == 1
	std::vector<unsigned char> message { 0x99, (unsigned char)note, (unsigned char)velocity };
	p->sendMessage(&message);
#endif
}

RtMidiIn * allocate_midi_input_port()
{
#if HAVE_RTMIDI == 1
	auto *p = new RtMidiIn(RtMidi::Api::UNSPECIFIED, PROG_NAME);
	p->openVirtualPort("input");
	p->ignoreTypes(true, true, true);  // ignore sysex, timing, and active sensing messages
	return p;
#else
	return nullptr;
#endif
}

std::vector<unsigned char> receive_midi_note(RtMidiIn *const p)
{
	std::vector<unsigned char> message;
#if HAVE_RTMIDI == 1
	p->getMessage(&message);
#endif
	return message;
}

void close_midi_in_port(RtMidiIn *const midi_port)
{
#if HAVE_RTMIDI == 1
	delete midi_port;
#endif
}

void close_midi_out_port(RtMidiOut *const midi_port)
{
#if HAVE_RTMIDI == 1
	delete midi_port;
#endif
}
