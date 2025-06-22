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

#include "gui.h"
#include "io.h"
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

std::mutex ttf_lock;

TTF_Font * load_font(const std::string & filename, unsigned int font_height, bool fast_rendering)
{
        char *const real_path = realpath(filename.c_str(), NULL);

        ttf_lock.lock();
        TTF_Font *font = TTF_OpenFont(real_path, font_height);
	if (!font)
		printf("Font error: %s\n", TTF_GetError());

        if (!fast_rendering)
                TTF_SetFontHinting(font, TTF_HINTING_LIGHT);

        ttf_lock.unlock();

        free(real_path);

        return font;
}

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
	int h_offset       = h * 15 / 100;
	int channel_height = (h - h_offset) / channel_count;

	std::vector<clickable> clickables;

	for(int i=0; i<channel_count; i++) {
		int x = w - channel_width;
		int y = i * channel_height + h_offset;
		clickable c { };
		c.where    = { x, y, channel_width, channel_height };
		c.selected = false;
		clickables.push_back(c);
	}

	return clickables;
}

std::vector<clickable> generate_menu_button(const int w, const int h)
{
	int menu_button_width  = w * 10 / 100;
	int menu_button_height = h * 10 / 100;

	std::vector<clickable> clickables;

	int x = w - menu_button_width;
	int y = 0;
	clickable c { };
	c.where    = { x, y, menu_button_width, menu_button_height };
	c.selected = false;
	c.text     = "menu";
	clickables.push_back(c);

	return clickables;
}

std::vector<clickable> generate_pattern_grid(const int w, const int h, const int steps)
{
	int pattern_w   = w * 85 / 100;
	int pattern_h   = h * 95 / 100;
	int offset_h    = h * 5 / 100;

	int sq_steps    = sqrt(steps);
	int steps_w     = steps / sq_steps;
	int steps_h     = steps / steps_w;

	int step_width  = pattern_w / steps_w;
	int step_height = pattern_h / steps_h;

	std::vector<clickable> clickables;

	for(int i=0; i<steps; i++) {
		int x = (i % steps_w) * step_width;
		int y = (i / steps_w) * step_height + offset_h;
		clickable c { };
		c.where    = { x, y, step_width, step_height };
		c.selected = false;
		clickables.push_back(c);
	}

	return clickables;
}

void draw_text(TTF_Font *const font, SDL_Renderer *const screen, const int x, const int y, const std::string & text, const std::optional<std::pair<int, int> > & center_in)
{
	SDL_Surface *surface = TTF_RenderUTF8_Solid(font, text.c_str(), { 192, 255, 192, 255 });
	assert(surface);
	SDL_Texture *texture = SDL_CreateTextureFromSurface(screen, surface);
	assert(texture);

	Uint32 format = 0;
	int    access = 0;
	int    w      = 0;
	int    h      = 0;
	SDL_QueryTexture(texture, &format, &access, &w, &h);

	SDL_Rect dest { };
	if (center_in.has_value()) {
		dest.x = x + center_in.value().first  / 2 - w / 2;
		dest.y = y + center_in.value().second / 2 - h / 2;
	}
	else {
		dest.x = x;
		dest.y = y;
	}
	dest.w = w;
	dest.h = h;
	SDL_RenderCopy(screen, texture, nullptr, &dest);

	SDL_DestroyTexture(texture);
	SDL_FreeSurface   (surface);
}

void draw_clickables(TTF_Font *const font, SDL_Renderer *const screen, const std::vector<clickable> & clickables, const std::optional<size_t> hl_index, const std::optional<size_t> play_index)
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

		if (clickables[i].text.empty() == false) {
			draw_text(font, screen, x1, y1, clickables[i].text, { { clickables[i].where.w, clickables[i].where.h } });
		}
	}
}

struct sample
{
	sound_sample *s;
	std::string   name;
};

int main(int argc, char *argv[])
{
	constexpr const int sample_rate = 44100;
	init_pipewire(&argc, &argv);
	sound_parameters sound_pars(sample_rate, 2);
	configure_pipewire_audio(&sound_pars);
	sound_pars.global_volume = 1.;

	signal(SIGTERM, sigh);
	atexit(SDL_Quit);

	TTF_Init();

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

	TTF_Font *font = load_font("/usr/share/fonts/truetype/freefont/FreeSans.ttf", h * 5 / 100, false);
	assert(font);

	if (full_screen)
		SDL_ShowCursor(SDL_DISABLE);

	bool redraw = true;
	int  steps  = 16;
	int  bpm    = 135;

	enum { m_pattern, m_menu } mode               = m_pattern;
	std::array<std::vector<clickable>, pattern_groups> pat_clickables;
	std::optional<size_t>  pat_clickable_selected;
	size_t                 pattern_group          = 0;

	std::vector<clickable> channel_clickables     = generate_channel_column(w, h, pattern_groups);

	std::vector<clickable> menu_button_clickables = generate_menu_button(w, h);

	for(size_t i=0; i<pattern_groups; i++)
		pat_clickables[i] = generate_pattern_grid(w, h, steps);

	std::array<sample, pattern_groups> samples { };
	for(size_t i=0; i<pattern_groups; i++) {
		samples[i].name = i & 1 ? "studio-hihat-sound-a-key-05-yvg.wav" : "small-reverb-bass-drum-sound-a-key-10-G8d.wav";
		samples[i].s    = new sound_sample(sample_rate, samples[i].name);
		samples[i].s->add_mapping(0, 0, 1.0);  // mono -> left
		samples[i].s->add_mapping(0, 1, 1.0);  // mono -> right
	}

	read_file("default.kaboem", &pat_clickables, &bpm);

	int    sleep_ms       = 60 * 1000 / bpm;
	size_t prev_pat_index = size_t(-1);

	while(!do_exit) {
		size_t pat_index = get_ms() / sleep_ms % steps;
		if (pat_index != prev_pat_index) {
			std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
			for(size_t i=0; i<pattern_groups; i++) {
				if (pat_clickables[i][pat_index].selected)
					sound_pars.sounds.push_back({ samples[i].s, 0 });
			}
			lck.unlock();
			
			redraw = true;
			prev_pat_index = pat_index;
		}

		if (redraw) {
			SDL_SetRenderDrawColor(screen, 0, 0, 0, 255);
			SDL_RenderClear(screen);

			draw_clickables(font, screen, menu_button_clickables, { }, { });

			if (mode == m_pattern) {
				draw_clickables(font, screen, pat_clickables[pattern_group], pat_clickable_selected, pat_index);
				draw_clickables(font, screen, channel_clickables, { }, pattern_group);
				draw_text(font, screen, 0, h / 2 / 100, samples[pattern_group].name, { });
			}
			else if (mode == m_menu) {
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
				auto menu_clicked = find_clickable(menu_button_clickables, event.button.x, event.button.y);
				if (menu_clicked.has_value()) {
					if (mode == m_pattern)
						mode = m_menu;
					else
						mode = m_pattern;
				}
				else {
					auto new_group = find_clickable(channel_clickables, event.button.x, event.button.y);
					if (new_group.has_value()) {
						channel_clickables[pattern_group].selected = false;
						pattern_group = new_group.value();
						channel_clickables[pattern_group].selected = true;
					}
					else {
						pat_clickable_selected = find_clickable(pat_clickables[pattern_group], event.button.x, event.button.y);
					}
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

	pw_main_loop_quit(sound_pars.pw.loop);
	sound_pars.pw.th->join();
	delete sound_pars.pw.th;

	write_file("default.kaboem", pat_clickables, bpm);

//	unload_sample_cache();

	return 0;
}
