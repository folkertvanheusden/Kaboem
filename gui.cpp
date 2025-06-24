#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <csignal>
#include <ctime>
#include <optional>
#include <sndfile.h>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "gui.h"
#include "io.h"
#include "pipewire.h"
#include "pipewire-audio.h"
#include "player.h"
#include "sample.h"
#include "sound.h"


std::atomic_bool do_exit { false };

void sigh(int s)
{
	do_exit = true;
}

struct fileselector_data {
	std::mutex  lock;
	std::string file;
	bool        finished;
};

void fs_callback(void *userdata, const char * const *filelist, int filter)
{
	fileselector_data *fs_data = reinterpret_cast<fileselector_data *>(userdata);
	std::unique_lock<std::mutex> lck(fs_data->lock);
	if (filelist && filelist[0])
		fs_data->file = filelist[0];
	else
		fs_data->file.clear();
	fs_data->finished = true;
}

std::mutex ttf_lock;

TTF_Font * load_font(const std::string & filename, unsigned int font_height, bool fast_rendering)
{
        char *const real_path = realpath(filename.c_str(), NULL);

        ttf_lock.lock();
        TTF_Font *font = TTF_OpenFont(real_path, font_height);
	if (!font)
		printf("Font error: %s\n", SDL_GetError());

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

struct up_down_widget {
	size_t up;
	size_t down;
	size_t up_10;
	size_t down_10;
	int    x;
	int    y;
	int    text_w;
	int    text_h;
};

std::vector<clickable> generate_up_down_widget(const int w, const int h, int x, int y, const std::string & name, const size_t click_offset, up_down_widget *const pars)
{
	int menu_button_width  = w * 15 / 100;
	int menu_button_height = h * 15 / 100;

	pars->text_w = menu_button_width;
	pars->text_h = menu_button_height / 3;

	std::vector<clickable> clickables;

	y += menu_button_height;
	clickable cbpm { };
	cbpm.where    = { x, y, menu_button_width, menu_button_height / 3 };
	cbpm.text     = name;
	clickables.push_back(cbpm);
	y += menu_button_height / 3;
	cbpm.where    = { x, y, menu_button_width, menu_button_height / 3 };
	cbpm.text     = "↑";
	pars->up      = clickables.size() + click_offset;
	clickables.push_back(cbpm);
	y += menu_button_height / 3;
	cbpm.where    = { x, y, menu_button_width, menu_button_height / 3 };
	cbpm.text     = "↑↑↑";
	pars->up_10   = clickables.size() + click_offset;
	clickables.push_back(cbpm);
	y += menu_button_height / 3;
	pars->x       = x;
	pars->y       = y;
	y += menu_button_height / 3;
	cbpm.where    = { x, y, menu_button_width, menu_button_height / 3 };
	cbpm.text     = "↓";
	pars->down    = clickables.size() + click_offset;
	clickables.push_back(cbpm);
	y += menu_button_height / 3;
	cbpm.where    = { x, y, menu_button_width, menu_button_height / 3 };
	cbpm.text     = "↓↓↓";
	pars->down_10 = clickables.size() + click_offset;
	clickables.push_back(cbpm);
	y += menu_button_height / 3;

	return clickables;
}

std::vector<clickable> generate_menu_buttons(const int w, const int h, size_t *const pattern_load_idx, size_t *const save_idx, size_t *const clear_idx, size_t *const quit_idx, up_down_widget *const bpm_widget_pars, size_t *const record_idx, up_down_widget *const volume_widget_pars, size_t *const pause_idx)
{
	int menu_button_width  = w * 15 / 100;
	int menu_button_height = h * 15 / 100;

	std::vector<clickable> clickables;

	int x = 0;
	int y = 0;
	{
		clickable c { };
		c.where    = { x, y, menu_button_width, menu_button_height };
		c.text     = "pause";
		*pause_idx = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}
	{
		clickable c { };
		c.where    = { x, y, menu_button_width, menu_button_height };
		c.text     = "load";
		*pattern_load_idx  = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}
	{
		clickable c { };
		c.where    = { x, y, menu_button_width, menu_button_height };
		c.text     = "record";
		*record_idx  = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}
	{
		clickable c { };
		c.where    = { x, y, menu_button_width, menu_button_height };
		c.text     = "save";
		*save_idx  = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}
	{
		clickable c { };
		c.where    = { x, y, menu_button_width, menu_button_height };
		c.text     = "clear";
		*clear_idx  = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}
	{
		clickable c { };
		c.where    = { x, y, menu_button_width, menu_button_height };
		c.text     = "quit";
		*quit_idx  = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}

	std::vector<clickable> bpm_widget = generate_up_down_widget(w, h, 0, y, "BPM", clickables.size(), bpm_widget_pars);
	std::copy(bpm_widget.begin(), bpm_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> volume_widget = generate_up_down_widget(w, h, menu_button_width, y, "vol", clickables.size(), volume_widget_pars);
	std::copy(volume_widget.begin(), volume_widget.end(), std::back_inserter(clickables));

	return clickables;
}

std::vector<clickable> generate_sample_buttons(const int w, const int h, size_t *const sample_load_idx, up_down_widget *const vol_widget_left_pars, up_down_widget *const vol_widget_right_pars, up_down_widget *const midi_note_widget_pars)
{
	int menu_button_width  = w * 15 / 100;
	int menu_button_height = h * 15 / 100;

	std::vector<clickable> clickables;

	int x = 0;
	int y = 0;
	{
		clickable c { };
		c.where          = { x, y, menu_button_width, menu_button_height };
		c.text           = "load";
		*sample_load_idx = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}

	std::vector<clickable> vol_left_widget = generate_up_down_widget(w, h, 0, y, "left", clickables.size(), vol_widget_left_pars);
	std::copy(vol_left_widget.begin(), vol_left_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> vol_right_widget = generate_up_down_widget(w, h, menu_button_width, y, "right", clickables.size(), vol_widget_right_pars);
	std::copy(vol_right_widget.begin(), vol_right_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> midi_note_widget = generate_up_down_widget(w, h, menu_button_width * 2, y, "MIDI note", clickables.size(), midi_note_widget_pars);
	std::copy(midi_note_widget.begin(), midi_note_widget.end(), std::back_inserter(clickables));

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
	SDL_Surface *surface = TTF_RenderText_Solid(font, text.c_str(), 0, { 192, 255, 192, 255 });
	assert(surface);
	SDL_Texture *texture = SDL_CreateTextureFromSurface(screen, surface);
	assert(texture);

	SDL_FRect dest { };
	if (center_in.has_value()) {
		dest.x = x + center_in.value().first  / 2 - surface->w / 2;
		dest.y = y + center_in.value().second / 2 - surface->h / 2;
	}
	else {
		dest.x = x;
		dest.y = y;
	}
	dest.w = surface->w;
	dest.h = surface->h;
	SDL_RenderTexture(screen, texture, nullptr, &dest);

	SDL_DestroyTexture(texture);
	SDL_DestroySurface(surface);
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
		float                  x1 = clickables[i].where.x;
		float                  y1 = clickables[i].where.y;
		SDL_FRect              r    { x1, y1, float(clickables[i].where.w), float(clickables[i].where.h) };
		SDL_SetRenderDrawColor(screen, color[0], color[1], color[2], 255);
		SDL_RenderFillRect(screen, &r);
		SDL_SetRenderDrawColor(screen, 40, 40, 40, 191);
		SDL_RenderRect(screen, &r);

		if (clickables[i].text.empty() == false) {
			draw_text(font, screen, x1, y1, clickables[i].text, { { clickables[i].where.w, clickables[i].where.h } });
		}
	}
}

int main(int argc, char *argv[])
{
	init_pipewire(&argc, &argv);
	sound_parameters sound_pars(sample_rate, 2);
	configure_pipewire_audio(&sound_pars);
	sound_pars.global_volume = 1.;

	std::string path = get_current_dir_name();

	signal(SIGTERM, sigh);
	atexit(SDL_Quit);

	TTF_Init();

	bool full_screen = false;

	SDL_Init(SDL_INIT_VIDEO);

	SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
	if (display_id == 0) {
		SDL_Log("Failed to get primary display: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	const SDL_DisplayMode *display_mode = SDL_GetCurrentDisplayMode(display_id);
	if (display_mode == nullptr) {
		SDL_Log("Failed to get display mode: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	printf("%dx%d\n", display_mode->w, display_mode->h);

	SDL_SetHint(SDL_HINT_RENDER_DRIVER,      "software");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1"       );
	SDL_Window *win = SDL_CreateWindow("Kaboem",
                          display_mode->w, display_mode->h,
                          (full_screen ? SDL_WINDOW_FULLSCREEN: 0));
	assert(win);
	SDL_Renderer *screen = SDL_CreateRenderer(win, nullptr);
	assert(screen);

	TTF_Font *font = load_font("/usr/share/fonts/truetype/freefont/FreeSans.ttf", display_mode->h * 5 / 100, false);
	assert(font);

//	if (full_screen)
//		SDL_HideCursor();

	bool redraw = true;
	int  steps  = 16;
	int  bpm    = 135;
	int  vol    = 100;

	enum { m_pattern, m_menu, m_sample } mode      = m_pattern;
	enum { fs_load, fs_save, fs_none, fs_load_sample, fs_record } fs_action = fs_none;
	size_t fs_action_sample_index                  = 0;
	fileselector_data      fs_data { };
	std::shared_mutex      pat_clickables_lock;
	std::array<std::vector<clickable>, pattern_groups> pat_clickables;
	std::optional<size_t>  pat_clickable_selected;
	size_t                 pattern_group           = 0;

	std::vector<clickable> channel_clickables      = generate_channel_column(display_mode->w, display_mode->h, pattern_groups);

	std::vector<clickable> menu_button_clickables  = generate_menu_button(display_mode->w, display_mode->h);

	size_t         pattern_load_idx = 0;
	size_t         save_idx         = 0;
	size_t         clear_idx        = 0;
	size_t         quit_idx         = 0;
	up_down_widget bpm_widget         { };
	size_t         record_idx       = 0;
	size_t         pause_idx        = 0;
	up_down_widget vol_widget         { };
	std::vector<clickable> menu_buttons_clickables = generate_menu_buttons(display_mode->w, display_mode->h, &pattern_load_idx, &save_idx, &clear_idx, &quit_idx, &bpm_widget, &record_idx, &vol_widget, &pause_idx);
	std::string    menu_status;

	size_t         sample_load_idx        = 0;
	up_down_widget sample_vol_widget_left   { };
	up_down_widget sample_vol_widget_right  { };
	up_down_widget midi_note_widget_pars    { };
	std::vector<clickable> sample_buttons_clickables = generate_sample_buttons(display_mode->w, display_mode->h, &sample_load_idx, &sample_vol_widget_left, &sample_vol_widget_right, &midi_note_widget_pars);

	for(size_t i=0; i<pattern_groups; i++)
		pat_clickables[i] = generate_pattern_grid(display_mode->w, display_mode->h, steps);

	std::array<sample, pattern_groups> samples { };

	SDL_DialogFileFilter sf_filters[]        { { "Kaboem files", "kaboem"  } };
	SDL_DialogFileFilter sf_filters_sample[] { { "Samples",      "wav;mp3" } };
	SDL_DialogFileFilter sf_filters_record[] { { "Record",       "wav"     } };

	if (read_file("default.kaboem", &pat_clickables, &bpm, &samples)) {
		for(size_t i=0; i<pattern_groups; i++) {
			if (samples[i].name.empty() == false)
				channel_clickables[i].text = get_filename(samples[i].name).substr(0, 5);
		}
	}

	std::atomic_int  sleep_ms       = 60 * 1000 / bpm;
	size_t           prev_pat_index = size_t(-1);
	std::atomic_bool paused         = false;

	std::thread player_thread([&pat_clickables, &pat_clickables_lock, &samples, &sleep_ms, &sound_pars, &paused] {
			player(&pat_clickables, &pat_clickables_lock, &samples, &sleep_ms, &sound_pars, &paused, &do_exit);
			});

	while(!do_exit) {
		size_t pat_index = get_ms() / sleep_ms % steps;
		if (pat_index != prev_pat_index && !paused) {
			redraw = true;
			prev_pat_index = pat_index;
		}

		if (fs_action != fs_none) {
			std::unique_lock<std::mutex> lck(fs_data.lock);
			if (fs_action == fs_load) {
				if (fs_data.finished) {
					if (fs_data.file.empty() == false) {
						{
							std::unique_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
							read_file(fs_data.file, &pat_clickables, &bpm, &samples);
						}
						std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
						sleep_ms = 60 * 1000 / bpm;
						for(size_t i=0; i<pattern_groups; i++) {
							if (samples[i].name.empty() == false)
								channel_clickables[i].text = get_filename(samples[i].name).substr(0, 5);
						}
						redraw = true;
						menu_status = "file " + get_filename(fs_data.file) + " read";
					}
					fs_action = fs_none;
				}
			}
			else if (fs_action == fs_save) {
				if (fs_data.finished) {
					if (fs_data.file.empty() == false) {
						std::string file     = fs_data.file;
						size_t      file_len = file.size();
						if (file_len > 7 && file.substr(file_len - 7) != ".kaboem")
							file += ".kaboem";
						std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
						write_file(file, pat_clickables, bpm, samples);
						menu_status = "file " + get_filename(fs_data.file) + " written";
					}
					fs_action = fs_none;
				}
			}
			else if (fs_action == fs_load_sample) {
				if (fs_data.finished) {
					if (fs_data.file.empty() == false) {
						std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
						sample & s = samples[fs_action_sample_index];
						s.name = fs_data.file;
						delete s.s;

                                		s.s = new sound_sample(sample_rate, s.name);
						if (s.s->begin() == false) {
							delete s.s;
							s.s = nullptr;
						}

						if (s.s) {
							bool is_stereo = s.s->get_n_channels() >= 2;
							s.s->add_mapping(0, 0, 1.0);
							s.s->add_mapping(is_stereo ? 1 : 0, 1, 1.0);

							menu_status = "file " + get_filename(fs_data.file) + " read";
							channel_clickables[fs_action_sample_index].text = get_filename(s.name).substr(0, 5);
							printf("HIER %d\n", is_stereo);
						}
						else {
							menu_status = "file " + get_filename(fs_data.file) + " NOT FOUND";
						}
						redraw = true;
					}
					fs_action = fs_none;
				}
			}
			else if (fs_action == fs_record) {
				if (fs_data.finished) {
					std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
					SF_INFO si { };
					si.samplerate = sample_rate;
					si.channels   = 2;
					si.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
					sound_pars.record_handle = sf_open(fs_data.file.c_str(), SFM_WRITE, &si);
					if (sound_pars.record_handle)
						menu_buttons_clickables[record_idx].selected = true;
					else
						menu_status = "Cannot create " + fs_data.file;
					fs_action = fs_none;
				}
			}

			if (fs_data.finished && fs_data.file.empty() == false) {
				auto slash = fs_data.file.find_last_of('/');
				if (slash != std::string::npos)
					path = fs_data.file.substr(0, slash);
			}
		}

		if (redraw && fs_action == fs_none) {
			SDL_SetRenderDrawColor(screen, 0, 0, 0, 255);
			SDL_RenderClear(screen);

			int font_height = display_mode->h / 2 / 100;

			draw_clickables(font, screen, menu_button_clickables, { }, { });

			if (mode == m_pattern) {
				std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
				draw_clickables(font, screen, pat_clickables[pattern_group], pat_clickable_selected, pat_index);
				draw_clickables(font, screen, channel_clickables, { }, pattern_group);
				if (samples[pattern_group].name.empty() == false)
					draw_text(font, screen, 0, display_mode->h / 2 / 100, samples[pattern_group].name, { });
			}
			else if (mode == m_menu) {
				if (menu_status.empty() == false)
					draw_text(font, screen, 0, display_mode->h - font_height * 5, menu_status, { { display_mode->w, font_height } });
				draw_clickables(font, screen, channel_clickables, { }, pattern_group);
				draw_clickables(font, screen, menu_buttons_clickables, { }, { });
				draw_text(font, screen, bpm_widget.x, bpm_widget.y, std::to_string(bpm), { { bpm_widget.text_w, bpm_widget.text_h } });
				draw_text(font, screen, vol_widget.x, vol_widget.y, std::to_string(vol), { { vol_widget.text_w, vol_widget.text_h } });
			}
			else if (mode == m_sample) {
				std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
				int  vol_left  = 0;
				int  vol_right = 0;
				bool is_stereo = false;
				sound_sample *const s = samples[fs_action_sample_index].s;
				auto midi_note = samples[fs_action_sample_index].midi_note;
				if (s) {
					is_stereo = s->get_n_channels() >= 2;
					vol_left  = s->get_mapping_target_volume(0) * 100;
					vol_right = s->get_mapping_target_volume(1) * 100;
				}
				std::string name = samples[fs_action_sample_index].name;
				lck.unlock();
				if (name.empty() == false)
					draw_text(font, screen, 0, display_mode->h - font_height * 5, name, { { display_mode->w, font_height } });
				draw_clickables(font, screen, channel_clickables, { }, pattern_group);
				draw_clickables(font, screen, sample_buttons_clickables, { }, { });
				draw_text(font, screen, sample_vol_widget_left.x,  sample_vol_widget_left.y,  std::to_string(vol_left),
					{ { sample_vol_widget_left.text_w,  sample_vol_widget_left.text_h } });
				if (is_stereo) {
					draw_text(font, screen, sample_vol_widget_right.x, sample_vol_widget_right.y, std::to_string(vol_right),
						{ { sample_vol_widget_right.text_w, sample_vol_widget_right.text_h } });
				}
				if (midi_note.has_value()) {
					draw_text(font, screen, midi_note_widget_pars.x, midi_note_widget_pars.y,  std::to_string(midi_note.value() + 1),
						{ { midi_note_widget_pars.text_w,  midi_note_widget_pars.text_h } });
				}
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
			if (event.type == SDL_EVENT_QUIT) {
				do_exit = true;
				break;
			}

			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
				if (mode == m_pattern) {
					auto menu_clicked = find_clickable(menu_button_clickables, event.button.x, event.button.y);
					if (menu_clicked.has_value()) {
						if (mode == m_pattern)
							mode = m_menu;
						else if (mode == m_sample)
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
							std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
							pat_clickable_selected = find_clickable(pat_clickables[pattern_group], event.button.x, event.button.y);
						}
					}
				}
				else if (mode == m_menu) {
					menu_status.clear();
					auto menu_clicked   = find_clickable(menu_button_clickables,  event.button.x, event.button.y);
					auto sample_clicked = find_clickable(channel_clickables,      event.button.x, event.button.y);
					auto menus_clicked  = find_clickable(menu_buttons_clickables, event.button.x, event.button.y);
					if (menu_clicked.has_value()) {
						if (mode == m_pattern)
							mode = m_menu;
						else
							mode = m_pattern;
					}
					else if (menus_clicked.has_value()) {
						size_t idx = menus_clicked.value();
						if (idx == clear_idx) {
							{
								std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
								write_file(path + "/before_clear.kaboem", pat_clickables, bpm, samples);
							}
							{
								std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
								sound_pars.sounds.clear();
							}
							{
								std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
								for(size_t i=0; i<pattern_groups; i++) {
									for(auto & element: pat_clickables[i])
										element.selected = false;

									{
										std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
										sample & s = samples[i];
										delete s.s;
										s.s = nullptr;
										s.name.clear();
									}
									channel_clickables[i].text.clear();
								}
							}
							menu_status = "cleared";
						}
						else if (idx == pattern_load_idx) {
							fs_data.finished = false;
							fs_action = fs_load;
							SDL_ShowOpenFileDialog(fs_callback, &fs_data, win, sf_filters, 1, path.c_str(), false);
						}
						else if (idx == save_idx) {
							fs_data.finished = false;
							fs_action = fs_save;
							SDL_ShowSaveFileDialog(fs_callback, &fs_data, win, sf_filters, 1, path.c_str());
						}
						else if (idx == quit_idx) {
							do_exit = true;
						}
						else if (idx == bpm_widget.up) {
							bpm++;
						}
						else if (idx == bpm_widget.up_10) {
							bpm += 10;
						}
						else if (idx == bpm_widget.down) {
							bpm = std::max(1, bpm - 1);
						}
						else if (idx == bpm_widget.down_10) {
							bpm = std::max(1, bpm - 10);
						}
						else if (idx == vol_widget.up) {
							vol = std::min(110, vol + 1);  // this one goes to 11
						}
						else if (idx == vol_widget.up_10) {
							vol = std::min(110, vol + 10);
						}
						else if (idx == vol_widget.down) {
							vol = std::max(0, vol - 1);
						}
						else if (idx == vol_widget.down_10) {
							vol = std::max(0, vol - 10);
						}
						else if (idx == record_idx) {
							std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
							if (sound_pars.record_handle) {
								sf_close(sound_pars.record_handle);
								sound_pars.record_handle = nullptr;
								menu_status = "recording stopped";
								menu_buttons_clickables[record_idx].selected = false;
							}
							else {
								lck.unlock();

								fs_data.finished = false;
								fs_action = fs_record;
								SDL_ShowSaveFileDialog(fs_callback, &fs_data, win, sf_filters_record, 1, path.c_str());
							}
						}
						else if (idx == pause_idx) {
							paused = !paused;
							menu_buttons_clickables[pause_idx].selected = paused;
						}
						sleep_ms                 = 60 * 1000 / bpm;
						std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
						sound_pars.global_volume = vol / 100.;
					}
					else if (sample_clicked.has_value()) {
						mode = m_sample;
						if (sample_clicked.has_value()) {
							channel_clickables[fs_action_sample_index].selected = false;
							fs_action_sample_index = sample_clicked.value();
							channel_clickables[fs_action_sample_index].selected = true;
							fs_data.finished = false;
						}
					}
				}
				else if (mode == m_sample) {
					auto menu_clicked   = find_clickable(menu_button_clickables,    event.button.x, event.button.y);
					auto sample_clicked = find_clickable(channel_clickables,        event.button.x, event.button.y);
					auto menus_clicked  = find_clickable(sample_buttons_clickables, event.button.x, event.button.y);
					if (menu_clicked.has_value()) {
						mode = m_menu;
						channel_clickables[fs_action_sample_index].selected = false;
					}
					else if (sample_clicked.has_value()) {
						channel_clickables[fs_action_sample_index].selected = false;
						fs_action_sample_index = sample_clicked.value();
						channel_clickables[fs_action_sample_index].selected = true;
						fs_data.finished = false;
					}
					else if (menus_clicked.has_value()) {
						size_t idx = menus_clicked.value();
						if (idx == sample_load_idx) {
							fs_data.finished = false;
							fs_action = fs_load_sample;
							SDL_ShowOpenFileDialog(fs_callback, &fs_data, win, sf_filters_sample, 1, path.c_str(), false);
						}
						else {
							std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
							sound_sample *const s = samples[fs_action_sample_index].s;
							auto & midi_note = samples[fs_action_sample_index].midi_note;
							bool is_stereo   = s ? s->get_n_channels() >= 2 : false;

							if (idx == midi_note_widget_pars.up) {
								if (midi_note.has_value() == false)
									midi_note = 0;
								else
									midi_note = std::min(127, midi_note.value() + 1);
							}
							else if (idx == midi_note_widget_pars.up_10) {
								if (midi_note.has_value() == false)
									midi_note = 0;
								else
									midi_note = std::min(127, midi_note.value() + 10);
							}
							else if (idx == midi_note_widget_pars.down) {
								if (midi_note.has_value() == false)
									midi_note = 127;
								else {
									midi_note = std::max(-1, midi_note.value() - 1);
									if (midi_note == -1)
										midi_note.reset();
								}
							}
							else if (idx == midi_note_widget_pars.down_10) {
								if (midi_note.has_value() == false)
									midi_note = 127;
								else {
									midi_note = std::max(-1, midi_note.value() - 10);
									if (midi_note == -1)
										midi_note.reset();
								}
							}
							else if (s == nullptr) {
								// skip volume when no sample
							}
							else if (idx == sample_vol_widget_left.up) {
								s->set_mapping_target_volume(0, std::min(1.1, s->get_mapping_target_volume(0) + 0.01));
								if (!is_stereo)
									s->set_mapping_target_volume(1, std::min(1.1, s->get_mapping_target_volume(1) + 0.01));
							}
							else if (idx == sample_vol_widget_left.up_10) {
								s->set_mapping_target_volume(0, std::min(1.1, s->get_mapping_target_volume(0) + 0.1));
								if (!is_stereo)
									s->set_mapping_target_volume(1, std::min(1.1, s->get_mapping_target_volume(1) + 0.1));
							}
							else if (idx == sample_vol_widget_left.down) {
								s->set_mapping_target_volume(0, std::max(0., s->get_mapping_target_volume(0) - 0.01));
								if (!is_stereo)
									s->set_mapping_target_volume(1, std::max(0., s->get_mapping_target_volume(1) - 0.01));
							}
							else if (idx == sample_vol_widget_left.down_10) {
								s->set_mapping_target_volume(0, std::max(0., s->get_mapping_target_volume(0) - 0.1));
								if (!is_stereo)
									s->set_mapping_target_volume(1, std::max(0., s->get_mapping_target_volume(1) - 0.1));
							}
							else if (is_stereo) {
								if (idx == sample_vol_widget_right.up) {
									s->set_mapping_target_volume(1, std::min(1.1, s->get_mapping_target_volume(1) + 0.01));
								}
								else if (idx == sample_vol_widget_right.up_10) {
									s->set_mapping_target_volume(1, std::min(1.1, s->get_mapping_target_volume(1) + 0.1));
								}
								else if (idx == sample_vol_widget_right.down) {
									s->set_mapping_target_volume(1, std::max(0., s->get_mapping_target_volume(1) - 0.01));
								}
								else if (idx == sample_vol_widget_right.down_10) {
									s->set_mapping_target_volume(1, std::max(0., s->get_mapping_target_volume(1) - 0.1));
								}
							}
						}
					}
				}
				redraw = true;
			}
			else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
				std::unique_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
				if (pat_clickable_selected.has_value()) {
					pat_clickables[pattern_group][pat_clickable_selected.value()].selected = !pat_clickables[pattern_group][pat_clickable_selected.value()].selected;
					pat_clickable_selected.reset();
					redraw = true;
				}
			}
		}
	}

	player_thread.join();

	pw_main_loop_quit(sound_pars.pw.loop);
	sound_pars.pw.th->join();
	delete sound_pars.pw.th;

	{  // stop any recording
		std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
		if (sound_pars.record_handle)
			sf_close(sound_pars.record_handle);
	}

	{
		std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
		write_file(path + "/default.kaboem", pat_clickables, bpm, samples);
	}

	unload_sample_cache();

	TTF_Quit();
	SDL_Quit();

	return 0;
}
