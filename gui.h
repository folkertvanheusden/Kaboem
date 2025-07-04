#pragma once

#include <optional>
#include <string>
#include <SDL3/SDL.h>

#include "sound.h"


#define PROG_NAME "Kaboem"
#define PROG_EXT  "kaboem"

struct clickable {
	SDL_Rect    where;
	bool        selected;
	std::string text;
	bool        without_bg;
};

struct pattern
{
	std::vector<clickable> pattern;
	std::vector<int>       note_delta;
	std::vector<double>    volume_left;
	std::vector<double>    volume_right;
	size_t                 dim;
};

struct sample
{
	sound_sample      *s;
	std::string        name;
	std::optional<int> midi_note;
};

constexpr const int    sample_rate     = 48000;
constexpr const size_t pattern_groups  = 8;
constexpr const size_t max_pattern_dim = 32;
constexpr const int    long_press_dt   = 500;
