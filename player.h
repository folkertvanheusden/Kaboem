#include <atomic>
#include <cstdint>

#include "gui.h"
#include "pipewire-audio.h"


uint64_t get_ms();
void player(const std::array<pattern, pattern_groups> *const pat_clickables, std::shared_mutex *const pat_clickables_lock,
		const std::array<sample, pattern_groups> *const samples,
		std::atomic_int *const sleep_ms, sound_parameters *const sound_pars,
		std::atomic_bool *const pause, std::atomic_bool *const do_exit,
		std::atomic_bool *const force_trigger,
		std::atomic_bool *const polyrythmic);
