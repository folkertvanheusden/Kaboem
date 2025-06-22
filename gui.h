#pragma once

#include <string>
#include <SDL2/SDL.h>


struct clickable {
	SDL_Rect    where;
	bool        selected;
	std::string text;
};

constexpr const size_t pattern_groups = 8;
