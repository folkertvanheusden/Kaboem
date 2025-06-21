#include <atomic>
#include <cassert>
#include <cmath>
#include <csignal>
#include <optional>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>


std::atomic_bool do_exit { false };

void sigh(int s)
{
	do_exit = true;
}

struct clickable {
	SDL_Rect where;
	bool     selected;
};

std::optional<size_t> find_clickable(const std::vector<clickable> & clickables, const int x, const int y)
{
	for(size_t i=0; i<clickables.size(); i++) {
		if (x >= clickables[i].where.x &&
		    y >= clickables[i].where.y &&
		    x < clickables[i].where.x + clickables[i].where.w &&
		    y < clickables[i].where.y + clickables[i].where.h) {
			return i;
		}
	}
	return { };
}

std::vector<clickable> generate_pattern_grid(const int w, const int h, const int steps)
{
	int pattern_w   = w * 85 / 100;
	int pattern_h   = h;

	int sq_steps    = sqrt(steps);
	int steps_w     = steps / sq_steps;
	int steps_h     = steps / steps_w;

	int step_width  = pattern_w / steps_w;
	int step_height = pattern_h / steps_h;

	std::vector<clickable> clickables;

	for(int i=0; i<steps; i++) {
		int x = (i % steps_w) * step_width;
		int y = (i / steps_w) * step_height;
		clickable c { };
		c.where    = { x, y, step_width, step_height };
		c.selected = false;
		clickables.push_back(c);
	}

	return clickables;
}

void draw_clickables(SDL_Renderer *const screen, const std::vector<clickable> & clickables, const std::optional<size_t> hl_index)
{
	for(size_t i=0; i<clickables.size(); i++) {
		bool hl = hl_index.has_value() == true && hl_index.value() == i;
		std::vector<int> color;
		if (clickables[i].selected)
			color = { 40, 255, hl ? 255 : 40 };
		else
			color = { 40, 100, hl ? 100 : 40 };
		int x1 = clickables[i].where.x;
		int y1 = clickables[i].where.y;
		int x2 = clickables[i].where.x + clickables[i].where.w;
		int y2 = clickables[i].where.y + clickables[i].where.h;
		boxRGBA(screen, x1, y1, x2, y2, color[0], color[1], color[2], 255);
		rectangleRGBA(screen, x1, y1, x2, y2, 40, 40, 40, 191);
	}
}

/*
	for(int cy=0; cy<n_rows; cy++)
		lineRGBA(screen, 0, cy * ysteps, w, cy * ysteps, 255, 255, 255, 255);
	for(int cx=0; cx<n_columns; cx++)
		lineRGBA(screen, cx * xsteps, 0, cx * xsteps, h, 255, 255, 255, 255);
*/

int main(int argc, char *argv[])
{
	signal(SIGTERM, sigh);
	atexit(SDL_Quit);

	int  display_nr  = 0;
	bool full_screen = false;
	int  create_w    = 1024;
	int  create_h    = 768;

	SDL_SetHint(SDL_HINT_RENDER_DRIVER,      "software");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1"       );
	SDL_Window *win = SDL_CreateWindow("Kaboem",
                          SDL_WINDOWPOS_UNDEFINED_DISPLAY(display_nr),
                          SDL_WINDOWPOS_UNDEFINED_DISPLAY(display_nr),
                          create_w, create_h,
                          (full_screen ? SDL_WINDOW_FULLSCREEN : 0) | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	assert(win);
	SDL_Renderer *screen = SDL_CreateRenderer(win, -1, 0);
	assert(screen);

	int w = 0;
	int h = 0;
	SDL_GetWindowSize(win, &w, &h);
	printf("%dx%d\n", w, h);

	if (full_screen)
		SDL_ShowCursor(SDL_DISABLE);

	bool redraw = true;
	int  steps  = 16;

	enum { m_pattern }     mode                   = m_pattern;
	std::vector<clickable> pat_clickables         = generate_pattern_grid(w, h, 16);
	std::optional<size_t>  pat_clickable_selected;

	while(!do_exit) {
		if (redraw) {
			printf("redraw\n");
			if (mode == m_pattern)
				draw_clickables(screen, pat_clickables, pat_clickable_selected);
			else {
				fprintf(stderr, "Internal error: %d\n", mode);
				break;
			}

			SDL_RenderPresent(screen);
			redraw = false;
		}

		SDL_Delay(10);

		SDL_Event event { 0 };
		if (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				do_exit = true;
				break;
			}

			if (event.type == SDL_MOUSEBUTTONDOWN) {
				pat_clickable_selected = find_clickable(pat_clickables, event.button.x, event.button.y);
				redraw = true;
			}
			else if (event.type == SDL_MOUSEBUTTONUP) {
				if (pat_clickable_selected.has_value()) {
					pat_clickables[pat_clickable_selected.value()].selected = !pat_clickables[pat_clickable_selected.value()].selected;
					pat_clickable_selected.reset();
					redraw = true;
				}
			}
			else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
				SDL_SetRenderDrawColor(screen, 0, 0, 0, 255);
				SDL_RenderClear(screen);
				redraw = true;
			}
		}
	}

	return 0;
}
