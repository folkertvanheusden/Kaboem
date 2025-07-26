#include <atomic>
#include <cstdint>

#include "gui.h"
#include "midi.h"
#include "player.h"
#include "sound.h"
#include "time.h"


void midi_processor(sound_parameters *const sound_pars, RtMidiIn *const midi_in, std::atomic_int *const percussion_midi_channel, std::atomic_bool *const midi_triggered, std::atomic_bool *const do_exit)
{
	if (!midi_in) {
		printf("No MIDI-in\n");
		return;
	}

	constexpr const int n_polyphonic = 10;
	pattern_midi patterns[n_polyphonic] { };
	int          indexes [n_polyphonic] { };
	for(int i=0; i<n_polyphonic; i++) {
		auto & p = patterns[i];
		p.note_delta   = { 0,  0  };
		p.volume_left  = { 1., 1. };
		p.volume_right = { 1., 1. };
		p.dim          = { 2      };
		p.serial_notes = { true   };
		p.swing        = { 0      };
		p.delay        = { 0      };
		p.current_note = { 255    };
		indexes[i]     = { -1     };
	}

	while(!*do_exit) {
		// TODO need a midi-waiter that is limited by time in RtMidi
		my_us_sleep(1000000 / (31250 / (10 * 2)));

		// check for midi events
		auto msg = receive_midi_note(midi_in);
		if (msg.size() != 3)
			continue;

		uint8_t cmd = msg.at(0) & 0xf0;
		uint8_t ch  = msg.at(0) & 0x0f;

		printf("MIDI in command %02x for channel %d\n", cmd, ch);

		if (*percussion_midi_channel != -1 && msg.at(0) == 0x90 + *percussion_midi_channel) {
			*midi_triggered = true;
			continue;
		}

		if ((cmd == 0x90 && msg.at(2) == 0) || cmd == 0x80) {
			bool immediately = msg.at(2) == 0;

			int found_idx = -1;
			for(int i=0; i<n_polyphonic; i++) {
				if (patterns[i].current_note == msg.at(1))
					found_idx = i;
			}

			if (found_idx != -1) {
				printf("Find note by index %d\n", found_idx);
				std::lock_guard<std::shared_mutex> lck(sound_pars->sounds_lock);
				for(size_t i=0; i<sound_pars->sounds.size(); i++) {
					auto & sound = sound_pars->sounds.at(i);
					if (sound.pat == &patterns[found_idx]) {
						if (immediately) {
							printf("Stop immediately\n");
							delete sound.bp_filter;
							sound_pars->sounds.erase(sound_pars->sounds.begin() + i);
							patterns[i].playing = false;
						}
						else {
							printf("End requested\n");
							sound.end_requested = true;
						}
						break;
					}
				}
			}
		}
		else if (cmd == 0x90) {
			double volume     = msg.at(2) / 127.;
			int    note_delta = sound_pars->midi_sample.midi_note.has_value() ? msg.at(1) - sound_pars->midi_sample.midi_note.value() : 0;
			printf("Queue %s with volume %f\n", sound_pars->midi_sample.name.c_str(), volume);

			int current_idx = -1;
			int new_idx     = -1;
			for(int i=0; i<n_polyphonic; i++) {
				if (patterns[i].current_note == msg.at(1) && patterns[i].playing == true)
					current_idx = i;
				else if (patterns[i].playing == false)
					new_idx = i;
			}

			if (current_idx != -1) {
				std::lock_guard<std::shared_mutex> lck(sound_pars->sounds_lock);
				for(size_t i=0; i<sound_pars->sounds.size(); i++) {
					auto & sound = sound_pars->sounds.at(i);
					if (sound.pat == &patterns[current_idx]) {
						sound.t = 0;
						break;
					}
				}
			}
			else if (new_idx != -1) {
				patterns[new_idx].current_note = msg.at(1);
				queue_sample(sound_pars, note_delta, volume, volume, &sound_pars->midi_sample, &patterns[new_idx], ++indexes[new_idx], nullptr);
			}
		}
	}
}
