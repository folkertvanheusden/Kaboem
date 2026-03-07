#include <atomic>
#include <cstdint>

#include "gui.h"
#include "midi.h"
#include "sound.h"
#include "time.h"


void midi_processor(sound_parameters *const sound_pars, midi_handle_wrapper_in & p, std::atomic_int *const percussion_midi_channel, std::atomic_bool *const midi_triggered, std::atomic_bool *const do_exit);

void midi_update_global_volume(sound_parameters *const sound_pars, const uint8_t volume_left, const uint8_t volume_right);
