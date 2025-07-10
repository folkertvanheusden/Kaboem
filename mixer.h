#include <atomic>

#include "sound.h"


void mixer(std::atomic_bool *const do_exit, sound_parameters *const sound_pars);
