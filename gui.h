#pragma once

#include <optional>
#include <string>
#include <SDL3/SDL.h>

#include "sound.h"


struct clickable {
	SDL_Rect    where;
	bool        selected;
	std::string text;
};

struct pattern
{
	std::vector<clickable> pattern;
	size_t                 dim;
};

struct sample
{
	sound_sample      *s;
	std::string        name;
	std::optional<int> midi_note;
};

constexpr const int    sample_rate     = 44100;
constexpr const size_t pattern_groups  = 8;
constexpr const size_t max_pattern_dim = 32;
