#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "gui.h"
#include "midi.h"
#include "sound.h"
#include "time.h"


void midi_processor(sound_parameters *const sound_pars, midi_handle_wrapper_in & p, std::atomic_int *const percussion_midi_channel, std::atomic_bool *const midi_triggered, std::atomic_bool *const do_exit);

void midi_update_global_volume(sound_parameters *const sound_pars, const uint8_t volume_left, const uint8_t volume_right);

struct midi_pump_data
{
	uint64_t                play_edge { 0 };
	uint64_t                delay_by  { 0 };
	std::condition_variable cv;
	std::mutex              lock;
	std::vector<std::pair<uint64_t, std::vector<uint8_t> > > midi_msgs;
};

extern midi_pump_data midi_pump;

void midi_queue_message(const uint64_t ts, const std::vector<uint8_t> msg);
void midi_sender       (sound_parameters *const sound_pars, std::atomic_bool *const do_exit);
