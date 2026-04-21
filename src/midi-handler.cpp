#include <atomic>
#include <cstdint>
#include <mutex>

#include "gui.h"
#include "midi.h"
#include "midi-handler.h"
#include "player.h"
#include "sound.h"
#include "time.h"

constexpr const int n_polyphonic = 10;
static pattern_midi patterns[n_polyphonic] { };
static int          indexes [n_polyphonic] { };
midi_pump_data      midi_pump;

void midi_queue_message(const uint64_t ts, const std::vector<uint8_t> msg)
{
	std::unique_lock<std::mutex> lck(midi_pump.lock);
	midi_pump.midi_msgs.push_back({ ts, msg });
}

void midi_sender(sound_parameters *const sound_pars, std::atomic_bool *const do_exit)
{
	auto midi_port = allocate_midi_output_port();

	while(!*do_exit) {
		std::unique_lock<std::mutex> lck(midi_pump.lock);
		if (midi_pump.cv.wait_for(lck, std::chrono::milliseconds(250)) == std::cv_status::timeout)
			continue;
		lck.unlock();

		SDL_DelayNS(midi_pump.delay_by * 1000ll);

		lck.lock();
		while(midi_pump.midi_msgs.empty() == false && midi_pump.midi_msgs.at(0).first <= midi_pump.play_edge) {
			uint64_t now = get_us();
			//
			// debug: show latency between queueing and playing
			//static uint64_t pt = 0;
			// printf("M: %zu | %zu %zu\n", midi_pump.midi_msgs.at(0).first, now - midi_pump.midi_msgs.at(0).first, now - pt);
			//pt = now;

#if HAVE_SMF == 1
			{
				std::unique_lock<std::mutex> lck(sound_pars->smf_lock);
				if (sound_pars->smf_track) {
					double when = (now - sound_pars->smf_start) / 1000000.;

					smf_event_t *event = smf_event_new_from_pointer(midi_pump.midi_msgs.at(0).second.data(), midi_pump.midi_msgs.at(0).second.size());
					smf_track_add_event_seconds(sound_pars->smf_track, event, when);
				}
			}
#endif

			if (midi_port.out)
				midi_port.out->send_message(midi_pump.midi_msgs.at(0).second);

			midi_pump.midi_msgs.erase(midi_pump.midi_msgs.begin() + 0);
		}
	}

	close_midi_out_port(midi_port);
}

void midi_update_global_volume(sound_parameters *const sound_pars, const uint8_t volume_left, const uint8_t volume_right)
{
	std::lock_guard<std::shared_mutex> lck(sound_pars->sounds_lock);

	for(auto & sound: sound_pars->sounds) {
		if (sound.pat >= &patterns[0] && sound.pat < &patterns[n_polyphonic]) {
			sound.volume_left  = volume_left  / 127.;
			sound.volume_right = volume_right / 127.;
		}
	}
}

void midi_processor(sound_parameters *const sound_pars, midi_handle_wrapper_in & midi_in, std::atomic_int *const percussion_midi_channel, std::atomic_bool *const midi_triggered, std::atomic_bool *const do_exit)
{
	if (midi_in.in == nullptr) {
		printf("No MIDI-in\n");
		return;
	}

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
		// check for midi events
		auto msg = receive_midi_note(250);
		if (msg.has_value() == false || msg.value().size() < 2)
			continue;

		uint8_t cmd = msg.value().at(0) & 0xf0;
		uint8_t ch  = msg.value().at(0) & 0x0f;

		printf("MIDI in command %02x for channel %d\n", cmd, ch);

		if (*percussion_midi_channel != -1 && msg.value().at(0) == 0x90 + *percussion_midi_channel) {
			*midi_triggered = true;
			continue;
		}

		if (((cmd == 0x90 && msg.value().at(2) == 0) || cmd == 0x80) && msg.value().size() == 3) {
			bool immediately = msg.value().at(2) == 0;

			int found_idx = -1;
			for(int i=0; i<n_polyphonic; i++) {
				if (patterns[i].current_note == msg.value().at(1))
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
							patterns[found_idx].playing = false;
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
		else if (cmd == 0x90 && msg.value().size() == 3) {
			double volume     = msg.value().at(2) / 127.;
			int    note_delta = sound_pars->midi_sample.midi_note.has_value() ? msg.value().at(1) - sound_pars->midi_sample.midi_note.value() : 0;
			printf("Queue %s with volume %f\n", sound_pars->midi_sample.name.c_str(), volume);

			int current_idx = -1;
			int new_idx     = -1;
			for(int i=0; i<n_polyphonic; i++) {
				if (patterns[i].current_note == msg.value().at(1) && patterns[i].playing == true)
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
				patterns[new_idx].current_note = msg.value().at(1);
				queue_sample(sound_pars, note_delta, volume, volume, &sound_pars->midi_sample, &patterns[new_idx], ++indexes[new_idx]);
			}
		}
		else if (cmd == 0xf0) {  // SysEx
			if (msg.value().size() == 8 && msg.value().at(1) == 0x7f &&  // realtime
					msg.value().at(2) == 0x7f &&  // any channel
					msg.value().at(3) == 0x04 &&  // device control
					msg.value().at(4) == 0x01)  // master volume
			{
				midi_update_global_volume(sound_pars, msg.value().at(6), msg.value().at(6));
			}
		}
	}
}
