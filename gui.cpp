#include <array>
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
#include "sample.h"
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

std::vector<clickable> generate_channel_column(const int w, const int h, const int channel_count)
{
	int channel_width  = w * 10 / 100;
	int channel_height = h / channel_count;

	std::vector<clickable> clickables;

	for(int i=0; i<channel_count; i++) {
		int x = w - channel_width;
		int y = i * channel_height;
		clickable c { };
		c.where    = { x, y, channel_width, channel_height };
		c.selected = false;
		clickables.push_back(c);
	}

	return clickables;
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

	sound_sample sample_kick(44100, "small-reverb-bass-drum-sound-a-key-10-G8d.wav");
	sample_kick.add_mapping(0, 0, 1.0);  // mono -> left
	sample_kick.add_mapping(0, 1, 1.0);  // mono -> right

	sound_sample sample_hihat(44100, "studio-hihat-sound-a-key-05-yvg.wav");
	sample_hihat.add_mapping(0, 0, 1.0);  // mono -> left
	sample_hihat.add_mapping(0, 1, 1.0);  // mono -> right

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
                          (full_screen ? SDL_WINDOW_FULLSCREEN : 0) | SDL_WINDOW_OPENGL);
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
	constexpr const size_t pattern_groups         = 8;
	std::array<std::vector<clickable>, pattern_groups> pat_clickables;
	std::optional<size_t>  pat_clickable_selected;
	size_t                 pattern_group          = 0;

	std::vector<clickable> channel_clickables     = generate_channel_column(w, h, pattern_groups);

	for(size_t i=0; i<pattern_groups; i++)
		pat_clickables[i] = generate_pattern_grid(w, h, steps);

	int    sleep_ms       = 60 * 1000 / bpm;
	size_t prev_pat_index = size_t(-1);

	while(!do_exit) {
		size_t pat_index = get_ms() / sleep_ms % steps;
		if (pat_index != prev_pat_index) {
			std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
			for(size_t i=0; i<pattern_groups; i++) {
				if (pat_clickables[i][pat_index].selected) {
					if (i & 1)
						sound_pars.sounds.push_back({ &sample_hihat, 0 });
					else
						sound_pars.sounds.push_back({ &sample_kick, 0 });
				}
			}
			lck.unlock();
			
			redraw = true;
			prev_pat_index = pat_index;
		}

		if (redraw) {
			if (mode == m_pattern) {
				draw_clickables(screen, pat_clickables[pattern_group], pat_clickable_selected, pat_index);
				draw_clickables(screen, channel_clickables, { }, pattern_group);
			}
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
				auto new_group = find_clickable(channel_clickables, event.button.x, event.button.y);
				if (new_group.has_value()) {
					channel_clickables[pattern_group].selected = false;
					pattern_group = new_group.value();
					channel_clickables[pattern_group].selected = true;
				}
				else {
					pat_clickable_selected = find_clickable(pat_clickables[pattern_group], event.button.x, event.button.y);
				}
				redraw = true;
			}
			else if (event.type == SDL_MOUSEBUTTONUP) {
				if (pat_clickable_selected.has_value()) {
					pat_clickables[pattern_group][pat_clickable_selected.value()].selected = !pat_clickables[pattern_group][pat_clickable_selected.value()].selected;
					pat_clickable_selected.reset();
					redraw = true;
				}
			}
		}
	}

	unload_sample_cache();

	return 0;
}
