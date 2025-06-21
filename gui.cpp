#include <atomic>
#include <cassert>
#include <cmath>
#include <csignal>
#include <ctime>
#include <optional>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "pipewire.h"
#include "pipewire-audio.h"
#include "sound.h"


std::atomic_bool do_exit { false };

void sigh(int s)
{
	do_exit = true;
}

uint64_t get_ms()
{
	timespec ts { };
	clock_gettime(CLOCK_REALTIME, &ts);
	return uint64_t(ts.tv_sec) * uint64_t(1000) + uint64_t(ts.tv_nsec / 1000000);
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

void draw_clickables(SDL_Renderer *const screen, const std::vector<clickable> & clickables, const std::optional<size_t> hl_index, const std::optional<size_t> play_index)
{
	for(size_t i=0; i<clickables.size(); i++) {
		bool hl = hl_index  .has_value() == true && hl_index  .value() == i;
		bool pl = play_index.has_value() == true && play_index.value() == i;
		std::vector<int> color;
		if (clickables[i].selected) {
			int sub_color = hl ? 255 : 40;
			if (pl)
				color = { 255, 40, sub_color };
			else
				color = { 40, 255, sub_color };
		}
		else {
			int sub_color = hl ? 100 : 40;
			if (pl)
				color = { 100, 40, sub_color };
			else
				color = { 40, 100, sub_color };
		}
		int x1 = clickables[i].where.x;
		int y1 = clickables[i].where.y;
		int x2 = clickables[i].where.x + clickables[i].where.w;
		int y2 = clickables[i].where.y + clickables[i].where.h;
		boxRGBA(screen, x1, y1, x2, y2, color[0], color[1], color[2], 255);
		rectangleRGBA(screen, x1, y1, x2, y2, 40, 40, 40, 191);
	}
}

int main(int argc, char *argv[])
{
	init_pipewire(&argc, &argv);
	sound_parameters sound_pars(44100, 2);
	configure_pipewire_audio(&sound_pars);
	sound_pars.global_volume = 1.;

	sound_sample sample(44100, "small-reverb-bass-drum-sound-a-key-10-G8d.wav");
	sample.add_mapping(0, 0, 1.0);  // mono -> left
	sample.add_mapping(0, 1, 1.0);  // mono -> right

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
	int  bpm    = 130;

	enum { m_pattern }     mode                   = m_pattern;
	std::vector<clickable> pat_clickables         = generate_pattern_grid(w, h, steps);
	std::optional<size_t>  pat_clickable_selected;

	int    sleep_ms       = 60 * 1000 / bpm;
	size_t prev_pat_index = size_t(-1);

	while(!do_exit) {
		size_t pat_index = get_ms() / sleep_ms % steps;
		if (pat_index != prev_pat_index) {
			std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
			if (pat_clickables[pat_index].selected)
				sound_pars.sounds.push_back({ &sample, 0 });
			lck.unlock();
			
			redraw = true;
			prev_pat_index = pat_index;
		}

		if (redraw) {
			if (mode == m_pattern)
				draw_clickables(screen, pat_clickables, pat_clickable_selected, pat_index);
			else {
				fprintf(stderr, "Internal error: %d\n", mode);
				break;
			}

			SDL_RenderPresent(screen);
			redraw = false;
		}

		SDL_Delay(1);

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
