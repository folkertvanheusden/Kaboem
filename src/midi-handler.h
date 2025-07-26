#include <atomic>
#include <cstdint>

#include "gui.h"
#include "midi.h"
#include "sound.h"
#include "time.h"


void midi_processor(sound_parameters *const sound_pars, RtMidiIn *const midi_in, std::atomic_int *const percussion_midi_channel, std::atomic_bool *const midi_triggered, std::atomic_bool *const do_exit);
