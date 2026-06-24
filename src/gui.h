#pragma once

#include <optional>
#include <string>
#include <SDL3/SDL.h>

#include "clickable.h"
#include "sound.h"


#define PROG_NAME "Kaboem"
#define PROG_EXT  "kaboem"

struct pattern
{
	std::vector<clickable> pattern;
	std::vector<int>       note_delta;
	std::vector<double>    volume_left;
	std::vector<double>    volume_right;
	size_t                 dim          { 0     };
	size_t                 wdim         { 0     };
	size_t                 hdim         { 0     };
	std::optional<int>     cursor;
	std::optional<double>  lp_cutoff;
	std::optional<double>  hp_cutoff;
	bool                   serial_notes { true  };
	int                    swing        { 0     };
	int                    delay        { 0     };
	std::atomic_bool       playing      { false };
};

struct pattern_midi : pattern
{
	uint8_t                current_note { 255  };
};

constexpr const int    sample_rate     = 48000;
constexpr const size_t pattern_groups  = 8;
constexpr const size_t max_pattern_dim = 32;
constexpr const int    long_press_dt   = 500;
constexpr const int    default_bpm     = 135;
