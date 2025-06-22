#pragma once

#include <string>
#include <SDL3/SDL.h>

#include "sound.h"


struct clickable {
	SDL_Rect    where;
	bool        selected;
	std::string text;
};

struct sample
{
	sound_sample *s;
	std::string   name;
};

constexpr const int    sample_rate    = 44100;
constexpr const size_t pattern_groups = 8;
