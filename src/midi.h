#include "config.h"
#if HAVE_RTMIDI == 1
#include <rtmidi/RtMidi.h>
#else
#define RtMidiOut void
#define RtMidiIn  void
#endif


bool init_midi  ();
void deinit_midi();

RtMidiOut * allocate_midi_output_port();
RtMidiIn  * allocate_midi_input_port ();
void                       send_midi_note   (RtMidiOut *const out_port, const int note, const int velocity);
std::vector<unsigned char> receive_midi_note(RtMidiIn  *const p);
void close_midi_in_port (RtMidiIn  *const midi_port);
void close_midi_out_port(RtMidiOut *const midi_port);
