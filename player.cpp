#include <array>
#include <atomic>
#include <cstdint>

#include "gui.h"
#include "pipewire-audio.h"


uint64_t get_ms()
{
	timespec ts { };
	clock_gettime(CLOCK_REALTIME, &ts);
	return uint64_t(ts.tv_sec) * uint64_t(1000) + uint64_t(ts.tv_nsec / 1000000);
}

void player(const std::array<std::vector<clickable>, pattern_groups> & pat_clickables, const std::array<sample, pattern_groups> & samples,
		std::atomic_int *const sleep_ms, sound_parameters *const sound_pars,
		std::atomic_bool *const pause, std::atomic_bool *const do_exit)
{
	size_t    prev_pat_index = size_t(-1);
	const int steps          = 16;

	while(!*do_exit) {
		if (*pause) {
			usleep(10000);
			continue;
		}

		size_t pat_index = get_ms() / *sleep_ms % steps;
		if (pat_index != prev_pat_index) {
			std::unique_lock<std::shared_mutex> lck(sound_pars->sounds_lock);

			for(size_t i=0; i<pattern_groups; i++) {
				if (pat_clickables[i][pat_index].selected && samples[i].s)
					sound_pars->sounds.push_back({ samples[i].s, 0 });
			}
			printf("%zu\n", sound_pars->sounds.size());
			lck.unlock();
			prev_pat_index = pat_index;
		}

		// TODO depending on next sample timestamp
		usleep(1000);
	}
}
