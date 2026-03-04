#include <atomic>
#include <cstdint>

#include "gui.h"


void queue_sample(sound_parameters *const sound_pars, const int note_delta, const double volume_left, const double volume_right,
		const sample *const s, pattern *const pat, const size_t pat_nr, RtMidiOut *const midi_port);

void player(std::array<pattern, pattern_groups> *const pat_clickables, std::shared_mutex *const pat_clickables_lock,
		const std::array<sample, pattern_groups> *const samples,
		std::atomic_int  *const sleep_ms, sound_parameters *const sound_pars,
		std::atomic_bool *const pause,    std::atomic_bool *const do_exit,
		std::atomic_bool *const force_trigger,
		std::atomic_bool *const polyrythmic,
		std::atomic_int  *const humanize_factor,
		std::atomic_uint64_t *const t_start);
