#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3_ttf/SDL_ttf.h>


// transform into a generic when a range must have other types as well (e.g. double/float)
class clickable {
private:
	bool        without_bg { false };
	char        key        { 0x00  };

public:
	SDL_Rect    where      {       };
	std::string text;
	bool        selected   { false };

public:
	clickable(const SDL_Rect & where, const std::string & text, const bool without_bg, const char key);
	clickable();
	virtual ~clickable();

	clickable operator=(const clickable & from);

	void draw(TTF_Font *const font_big, TTF_Font *const font_small, SDL_Renderer *const screen, const std::vector<int> & color) const;

	bool is_triggered(const SDL_Event & in) const;  // check mouse-click & keyboard events
};
