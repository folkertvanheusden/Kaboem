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

#include "font.h"
#include "frequencies.h"
#include "gui.h"
#include "io.h"
#include "midi.h"
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
	std::lock_guard<std::mutex> lck(fs_data->lock);
	if (filelist && filelist[0]) {
		char *temp = realpath(filelist[0], nullptr);
		if (temp) {
			fs_data->file = temp;
			free(temp);
		}
		else if (errno == ENOENT) {  // save
			fs_data->file = filelist[0];
		}
	}
	else {
		fs_data->file.clear();
	}
	fs_data->finished = true;
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

std::vector<clickable> generate_up_down_widget(const int w, const int h, int x, int y, const std::string & name, const size_t click_offset, up_down_widget *const pars, const bool step10 = true)
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
	if (step10) {
		cbpm.where    = { x, y, menu_button_width, menu_button_height / 3 };
		cbpm.text     = "↑↑↑";
		pars->up_10   = clickables.size() + click_offset;
		clickables.push_back(cbpm);
	}
	else {
		pars->up_10   = size_t(-1);
	}
	y += menu_button_height / 3;
	pars->x       = x;
	pars->y       = y;
	y += menu_button_height / 3;
	if (step10) {
		cbpm.where    = { x, y, menu_button_width, menu_button_height / 3 };
		cbpm.text     = "↓↓↓";
		pars->down_10 = clickables.size() + click_offset;
		clickables.push_back(cbpm);
	}
	else {
		pars->down_10 = size_t(-1);
	}
	y += menu_button_height / 3;
	cbpm.where    = { x, y, menu_button_width, menu_button_height / 3 };
	cbpm.text     = "↓";
	pars->down    = clickables.size() + click_offset;
	clickables.push_back(cbpm);
	y += menu_button_height / 3;

	return clickables;
}

std::vector<clickable> generate_menu_buttons(const int w, const int h, size_t *const pattern_load_idx, size_t *const save_idx, size_t *const clear_idx, size_t *const quit_idx, up_down_widget *const bpm_widget_pars, size_t *const record_idx, up_down_widget *const volume_widget_pars, size_t *const pause_idx, up_down_widget *const midi_ch_widget_pars, up_down_widget *const lp_filter_pars, up_down_widget *const hp_filter_pars, up_down_widget *const sound_saturation_pars, size_t *const polyrythmic_idx, up_down_widget *const swing_widget_pars)
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

	std::vector<clickable> volume_widget = generate_up_down_widget(w, h, menu_button_width, y, "volume", clickables.size(), volume_widget_pars);
	std::copy(volume_widget.begin(), volume_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> midi_ch_widget = generate_up_down_widget(w, h, menu_button_width * 2, y, "midi ch.", clickables.size(), midi_ch_widget_pars);
	std::copy(midi_ch_widget.begin(), midi_ch_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> lp_filter_pars_widget = generate_up_down_widget(w, h, menu_button_width * 3, y, "low pass", clickables.size(), lp_filter_pars);
	std::copy(lp_filter_pars_widget.begin(), lp_filter_pars_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> hp_filter_pars_widget = generate_up_down_widget(w, h, menu_button_width * 4, y, "high pass", clickables.size(), hp_filter_pars);
	std::copy(hp_filter_pars_widget.begin(), hp_filter_pars_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> sound_saturation_pars_widget = generate_up_down_widget(w, h, menu_button_width * 5, y, "saturation", clickables.size(), sound_saturation_pars);
	std::copy(sound_saturation_pars_widget.begin(), sound_saturation_pars_widget.end(), std::back_inserter(clickables));

	int up_down_height = menu_button_height / 3 * 6;
	x = 0;
	y += up_down_height;

	std::vector<clickable> swing_widget = generate_up_down_widget(w, h, x, y, "swing", clickables.size(), swing_widget_pars);
	std::copy(swing_widget.begin(), swing_widget.end(), std::back_inserter(clickables));
	y += up_down_height;

	{
		clickable c { };
		c.where          = { x, y + menu_button_height, menu_button_width, menu_button_height };
		c.text           = "polyrythmic";
		*polyrythmic_idx = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}

	return clickables;
}

std::vector<clickable> generate_sample_buttons(const int w, const int h, size_t *const sample_load_idx, up_down_widget *const vol_widget_left_pars, up_down_widget *const vol_widget_right_pars, up_down_widget *const midi_note_widget_pars, up_down_widget *const n_steps_pars, up_down_widget *const pitch_pars, size_t *const sample_unload_idx, size_t *const mute_idx)
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
	{
		clickable c { };
		c.where          = { x, y, menu_button_width, menu_button_height };
		c.text           = "unload";
		*sample_unload_idx = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}
	x += menu_button_width;
	x += menu_button_width;
	{
		clickable c { };
		c.where          = { x, y, menu_button_width, menu_button_height };
		c.text           = "mute";
		*mute_idx = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}

	std::vector<clickable> vol_left_widget = generate_up_down_widget(w, h, 0, y, "left", clickables.size(), vol_widget_left_pars);
	std::copy(vol_left_widget.begin(), vol_left_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> vol_right_widget = generate_up_down_widget(w, h, menu_button_width, y, "right", clickables.size(), vol_widget_right_pars);
	std::copy(vol_right_widget.begin(), vol_right_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> midi_note_widget = generate_up_down_widget(w, h, menu_button_width * 2, y, "MIDI note", clickables.size(), midi_note_widget_pars);
	std::copy(midi_note_widget.begin(), midi_note_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> n_steps_widget = generate_up_down_widget(w, h, menu_button_width * 3, y, "steps", clickables.size(), n_steps_pars, false);
	std::copy(n_steps_widget.begin(), n_steps_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> pitch_widget = generate_up_down_widget(w, h, menu_button_width * 4, y, "pitch", clickables.size(), pitch_pars, true);
	std::copy(pitch_widget.begin(), pitch_widget.end(), std::back_inserter(clickables));

	return clickables;
}

pattern generate_pattern_grid(const int w, const int h, const int steps)
{
	int pattern_w   = w * 85 / 100;
	int pattern_h   = h * 95 / 100;
	int offset_h    = h * 5 / 100;

	int steps_sq    = ceil(sqrt(steps));
	int step_width  = pattern_w / steps_sq;
	int step_height = pattern_h / steps_sq;

	pattern p;
	p.pattern   .resize(max_pattern_dim);
	p.note_delta.resize(max_pattern_dim);
	p.dim = steps;

	for(int i=0; i<steps; i++) {
		int x = (i % steps_sq) * step_width;
		int y = (i / steps_sq) * step_height + offset_h;
		clickable c { };
		c.where            = { x, y, step_width, step_height };
		c.selected         = false;
		p.pattern.at(i)    = c;
		p.note_delta.at(i) = 0;
	}

	return p;
}

void regenerate_pattern_grid(const int w, const int h, pattern *const p)
{
	int pattern_w   = w * 85 / 100;
	int pattern_h   = h * 95 / 100;
	int offset_h    = h * 5 / 100;

	int steps_sq    = ceil(sqrt(p->dim));
	printf("%zu: %d\n", p->dim, steps_sq);
	int step_width  = pattern_w / steps_sq;
	int step_height = pattern_h / steps_sq;

	for(size_t i=0; i<p->dim; i++) {
		int x = (i % steps_sq) * step_width;
		int y = (i / steps_sq) * step_height + offset_h;
		p->pattern.at(i).where = { x, y, step_width, step_height };
	}
}

void draw_text(TTF_Font *const font, SDL_Renderer *const screen, const int x, const int y, const std::string & text, const std::optional<std::pair<int, int> > & center_in, const bool important = false)
{
	SDL_Surface *surface = nullptr;
	if (important)
		surface = TTF_RenderText_Solid(font, text.c_str(), 0, { 255, 192, 192, 255 });
	else
		surface = TTF_RenderText_Solid(font, text.c_str(), 0, { 192, 255, 192, 255 });
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

void draw_clickables(TTF_Font *const font, SDL_Renderer *const screen, const std::vector<clickable> & clickables, const std::optional<size_t> hl_index, const std::optional<size_t> play_index, const ssize_t draw_limit = -1)
{
	size_t draw_n = draw_limit == -1 ? clickables.size() : draw_limit;

	for(size_t i=0; i<draw_n; i++) {
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

void set_filter_cutoff(sound_parameters *const sound_pars, filter_butterworth **const p, const bool is_high_pass, const std::optional<double> frequency)
{
	std::lock_guard<std::shared_mutex> lck(sound_pars->sounds_lock);

	if (frequency.has_value()) {
		if (!*p)
			*p = new filter_butterworth(sample_rate, is_high_pass, sqrt(2.));
		(*p)->configure(frequency.value());
	}
	else {
		delete *p;
		*p = nullptr;
	}
}

bool configure_filter(sound_parameters *const sound_pars, const up_down_widget & widget, const size_t widget_idx, const bool is_highpass, std::optional<double> *const f, const bool shift)
{
	int mul = shift ? 3 : 1;

	if (widget_idx == widget.up) {
		if (f->has_value() == false)
			*f = 1.;
		else
			*f = std::min(sample_rate / 2., f->value() + 20 * mul);
	}
	else if (widget_idx == widget.up_10) {
		if (f->has_value() == false)
			*f = 1.;
		else
			*f = std::min(sample_rate / 2., f->value() + 1000 * mul);
	}
	else if (widget_idx == widget.down) {
		if (f->has_value() == false)
			*f = sample_rate / 2.;
		else {
			*f = std::max(0., f->value() - 20 * mul);
			if (*f < 1.)
				f->reset();
		}
	}
	else if (widget_idx == widget.down_10) {
		if (f->has_value() == false)
			*f = sample_rate / 2.;
		else {
			*f = std::max(0., f->value() - 1000 * mul);
			if (*f < 1.)
				f->reset();
		}
	}
	else {
		return false;
	}

	if (is_highpass)
		set_filter_cutoff(sound_pars, &sound_pars->filter_hp, is_highpass, *f);
	else
		set_filter_cutoff(sound_pars, &sound_pars->filter_lp, is_highpass, *f);

	return true;
}

bool set_up_down_value(const size_t idx, const up_down_widget & widget, const int min_value, const int max_value, int *const value, const bool shift)
{
	int mul = shift ? 3 : 1;

	if (idx == widget.up)
		(*value) = std::min(max_value, *value + 1 * mul);
	else if (idx == widget.up_10)
		(*value) = std::min(max_value, *value + 10 * mul);
	else if (idx == widget.down)
		(*value) = std::max(min_value, (*value) - 1 * mul);
	else if (idx == widget.down_10)
		(*value) = std::max(min_value, (*value) - 10 * mul);
	else {
		return false;
	}

	return true;
}

bool set_up_down_value(const size_t idx, const up_down_widget & widget, const int min_value, const int max_value, std::optional<int> *const value, const bool shift)
{
	int mul = shift ? 3 : 1;

	if (idx == widget.up) {
		if (value->has_value() == false)
			*value = min_value;
		else
			*value = std::min(max_value, value->value() + 1 * mul);
	}
	else if (idx == widget.up_10) {
		if (value->has_value() == false)
			*value = min_value;
		else
			*value = std::min(max_value, value->value() + 10 * mul);
	}
	else if (idx == widget.down) {
		if (value->has_value() == false)
			*value = max_value;
		else {
			*value = std::max(min_value - 1, value->value() - 1 * mul);
			if (*value == min_value - 1)
				value->reset();
		}
	}
	else if (idx == widget.down_10) {
		if (value->has_value() == false)
			*value = max_value;
		else {
			*value = std::max(min_value - 1, value->value() - 10 * mul);
			if (*value == min_value - 1)
				value->reset();
		}
	}
	else {
		return false;
	}

	return true;
}

bool configure_volume(sound_parameters *const sound_pars, const up_down_widget & widget, const size_t widget_idx, sound_sample *const s, const int channel_index, const bool shift)
{
	int mul = shift ? 3 : 1;

	if (widget_idx == widget.up)
		s->set_mapping_target_volume(channel_index, std::min(1.1, s->get_mapping_target_volume(channel_index) + 0.01 * mul));
	else if (widget_idx == widget.up_10)
		s->set_mapping_target_volume(channel_index, std::min(1.1, s->get_mapping_target_volume(channel_index) + 0.1 * mul));
	else if (widget_idx == widget.down)
		s->set_mapping_target_volume(channel_index, std::max(0., s->get_mapping_target_volume(channel_index) - 0.01 * mul));
	else if (widget_idx == widget.down_10)
		s->set_mapping_target_volume(channel_index, std::max(0., s->get_mapping_target_volume(channel_index) - 0.1 * mul));
	else
		return false;

	return true;
}

void reset_pattern(std::array<pattern, pattern_groups> *const pat_clickables, const size_t pattern_group, sound_sample *const s, const bool zero)
{
	auto & pattern = (*pat_clickables)[pattern_group];

	for(size_t i=0; i<pattern.pattern.size(); i++) {
		if (zero)
			pattern.note_delta[i] = 0;

		std::string name = midi_note_to_name(s->get_base_midi_note() + pattern.note_delta[i]);
		pattern.pattern[i].text = name;
	}
}

void reset_all_patterns(std::array<pattern, pattern_groups> *const pat_clickables, std::shared_mutex *const pat_clickables_lock, const std::array<sample, pattern_groups> & samples, const bool zero)
{
	for(size_t i=0; i<pattern_groups; i++) {
		if (samples[i].s)
			reset_pattern(pat_clickables, i, samples[i].s, zero);
	}
}

void draw_please_wait(TTF_Font *const font, SDL_Renderer *const screen, const SDL_DisplayMode *const display_mode)
{
	draw_text(font, screen, 0, 0, "Please wait", { { display_mode->w, display_mode->h } }, true);
	SDL_RenderPresent(screen);
}

void do_error_message(TTF_Font *const font, SDL_Renderer *const screen, const SDL_DisplayMode *const display_mode, const std::string & error)
{
	int dim_w = display_mode->w / 6;
	int dim_h = display_mode->h / 6;

	SDL_FRect r { float(dim_w), float(dim_h), float(display_mode->w - dim_w * 2), float(display_mode->h - dim_h * 2) };
	SDL_SetRenderDrawColor(screen, 50, 40, 40, 255);
	SDL_RenderFillRect(screen, &r);
	SDL_SetRenderDrawColor(screen, 40, 40, 40, 191);
	SDL_RenderRect(screen, &r);

	SDL_SetRenderDrawColor(screen, 255, 40, 40, 255);
	draw_text(font, screen, 0, 0, error, { { display_mode->w, display_mode->h } }, true);
	SDL_RenderPresent(screen);

	for(;;) {
		SDL_Event event { };
		if (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_DOWN)
				break;
		}

		SDL_Delay(5);
	}
}

int main(int argc, char *argv[])
{
	int pw_argc = 1;
	init_pipewire(&pw_argc, &argv);

	bool full_screen = true;

	int c = -1;
	while((c = getopt(argc, argv, "-w")) != -1) {
		if (c == 'w')
			full_screen = false;
		else {
			fprintf(stderr, "\"-%c\" is not understood\n", c);
			return 1;
		}
	}

	sound_parameters sound_pars(sample_rate, 2);
	configure_pipewire_audio(&sound_pars);
	sound_pars.global_volume = 1.;

	srand(time(nullptr));

	const std::string path      = get_current_dir_name();
	std::string       work_path = path;

	auto midi_in = allocate_midi_input_port();

	signal(SIGTERM, sigh);
	atexit(SDL_Quit);

	init_fonts();

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

//	SDL_SetHint(SDL_HINT_RENDER_DRIVER,      "software");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1"       );
	SDL_Window *win = SDL_CreateWindow(PROG_NAME,
                          display_mode->w, display_mode->h,
                          (full_screen ? SDL_WINDOW_FULLSCREEN: 0));
	assert(win);
	SDL_Renderer *screen = SDL_CreateRenderer(win, nullptr);
	assert(screen);

	unsigned  font_height = display_mode->h * 5 / 100;
	TTF_Font *font        = load_font({ "DejaVu Sans", "Ubuntu Sans Regular", "Free Sans" }, font_height, false);
	assert(font);

	bool redraw = true;
	int  steps  = 16;
	int  bpm    = 135;
	int  vol    = 100;

	enum { m_pattern, m_menu, m_sample } mode      = m_pattern;
	enum { fs_load, fs_save, fs_none, fs_load_sample, fs_record } fs_action = fs_none;
	size_t fs_action_sample_index                  = 0;
	fileselector_data      fs_data { };
	std::shared_mutex      pat_clickables_lock;
	std::array<pattern, pattern_groups> pat_clickables { };
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
	up_down_widget midi_ch_widget     { };
	up_down_widget lp_filter_widget   { };
	up_down_widget hp_filter_widget   { };
	up_down_widget sound_saturation_widget { };
	int            sound_saturation = 0;
	std::optional<double> lp_filter_f;
	std::optional<double> hp_filter_f;
	std::optional<int> selected_midi_channel;
	size_t         polyrythmic_idx  = 0;
	up_down_widget swing_widget       { };
	std::vector<clickable> menu_buttons_clickables = generate_menu_buttons(display_mode->w, display_mode->h, &pattern_load_idx, &save_idx, &clear_idx, &quit_idx, &bpm_widget, &record_idx, &vol_widget, &pause_idx, &midi_ch_widget, &lp_filter_widget, &hp_filter_widget, &sound_saturation_widget, &polyrythmic_idx, &swing_widget);
	std::string    menu_status;
	std::atomic_bool polyrythmic    = false;
	int            swing_amount     = 0;

	size_t         sample_load_idx        = 0;
	size_t         sample_unload_idx      = 0;
	size_t         mute_idx               = 0;
	up_down_widget sample_vol_widget_left   { };
	up_down_widget sample_vol_widget_right  { };
	up_down_widget midi_note_widget_pars    { };
	up_down_widget n_steps_pars             { };
	up_down_widget pitch_pars               { };
	std::vector<clickable> sample_buttons_clickables = generate_sample_buttons(display_mode->w, display_mode->h, &sample_load_idx, &sample_vol_widget_left, &sample_vol_widget_right, &midi_note_widget_pars, &n_steps_pars, &pitch_pars, &sample_unload_idx, &mute_idx);

	for(size_t i=0; i<pattern_groups; i++)
		pat_clickables[i] = generate_pattern_grid(display_mode->w, display_mode->h, steps);

	std::array<sample, pattern_groups> samples { };

	SDL_DialogFileFilter sf_filters[]        { { "Kaboem files", PROG_EXT  } };
	SDL_DialogFileFilter sf_filters_sample[] { { "Samples",      "wav;mp3" } };
	SDL_DialogFileFilter sf_filters_record[] { { "Record",       "wav"     } };

	const std::vector<file_parameter> file_parameters {
		{ "bpm",          file_parameter::T_INT,    &bpm,              nullptr,                nullptr, nullptr,      nullptr, nullptr      },
		{ "volume",       file_parameter::T_INT,    &vol,              nullptr,                nullptr, nullptr,      nullptr, nullptr      },
		{ "saturation",   file_parameter::T_INT,    &sound_saturation, nullptr,                nullptr, nullptr,      nullptr, nullptr      },
		{ "midi-channel", file_parameter::T_INT,    nullptr,           &selected_midi_channel, nullptr, nullptr,      nullptr, nullptr      },
		{ "swing-factor", file_parameter::T_INT,    &swing_amount,     nullptr,                nullptr, nullptr,      nullptr, nullptr      },
		{ "lp-filter",    file_parameter::T_FLOAT,  nullptr,           nullptr,                nullptr, &lp_filter_f, nullptr, nullptr      },
		{ "hp-filter",    file_parameter::T_FLOAT,  nullptr,           nullptr,                nullptr, &hp_filter_f, nullptr, nullptr      },
		{ "polyrythmic",  file_parameter::T_ABOOL,  nullptr,           nullptr,                nullptr, nullptr,      nullptr, &polyrythmic }
	};

	std::atomic_int swing_amount_parameter { swing_amount };
	if (read_file("default." PROG_EXT, &pat_clickables, &samples, &file_parameters)) {
		for(size_t i=0; i<pattern_groups; i++) {
			if (samples[i].name.empty() == false)
				channel_clickables[i].text = get_filename(samples[i].name).substr(0, 5);
		}

		sound_pars.global_volume    = vol / 100.;
		sound_pars.sound_saturation = 1. - sound_saturation / 1000.;
		menu_buttons_clickables[polyrythmic_idx].selected = polyrythmic;
		regenerate_pattern_grid(display_mode->w, display_mode->h, &pat_clickables[pattern_group]);
		swing_amount_parameter      = swing_amount;

		reset_all_patterns(&pat_clickables, &pat_clickables_lock, samples, false);
	}

	std::atomic_int  sleep_ms       = 60 * 1000 / bpm;
	size_t           prev_pat_index = size_t(-1);
	std::atomic_bool paused         = false;
	std::atomic_bool force_trigger  = false;
	bool             shift          = false;

	std::thread player_thread([&pat_clickables, &pat_clickables_lock, &samples, &sleep_ms, &sound_pars, &paused, &force_trigger, &polyrythmic, &swing_amount_parameter] {
			player(&pat_clickables, &pat_clickables_lock, &samples, &sleep_ms, &sound_pars, &paused, &do_exit, &force_trigger, &polyrythmic, &swing_amount_parameter);
			});

	while(!do_exit) {
		// determine pattern index
		size_t pat_index = 0;
		{
			auto   now         = get_ms();
			std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
			size_t current_dim = pat_clickables[pattern_group].dim;

                        if (polyrythmic)
				pat_index = now / sleep_ms % current_dim;
			else {
				size_t max_steps = 0;
                                for(size_t i=0; i<pattern_groups; i++) {
                                        if (samples[i].s != nullptr)
                                                max_steps = std::max(max_steps, pat_clickables[i].dim);
                                }
				pat_index = size_t(now / double(sleep_ms) * current_dim / double(max_steps)) % current_dim;
                        }
		}
		if (pat_index != prev_pat_index && !paused) {
			redraw = true;
			prev_pat_index = pat_index;
		}

		// check for midi events
		if (midi_in.first && snd_seq_event_input_pending(midi_in.first, 1) != 0) {
			snd_seq_event_t *ev { nullptr };
			snd_seq_event_input(midi_in.first, &ev);
			if (ev->type == SND_SEQ_EVENT_NOTEON) {
				uint8_t ch = ev->data.note.channel;
				if (selected_midi_channel.has_value() && ch == selected_midi_channel) {
					std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
					pat_clickables[pattern_group].pattern[pat_index].selected = true;
					redraw = true;
				}
			}
		}

		// did the user select a file in the fileselector?
		if (fs_action != fs_none) {
			std::lock_guard<std::mutex> fs_lck(fs_data.lock);
			if (fs_action == fs_load) {
				if (fs_data.finished) {
					if (fs_data.file.empty() == false) {
						draw_please_wait(font, screen, display_mode);

						std::unique_lock<std::shared_mutex> lck    (sound_pars.sounds_lock);
						std::unique_lock<std::shared_mutex> pat_lck(pat_clickables_lock   );
						if (read_file(fs_data.file, &pat_clickables, &samples, &file_parameters)) {
							sound_pars.global_volume                          = vol / 100.;
							sound_pars.sound_saturation                       = 1. - sound_saturation / 1000.;
							swing_amount_parameter                            = swing_amount;
							sleep_ms                                          = 60 * 1000 / bpm;
							menu_buttons_clickables[polyrythmic_idx].selected = polyrythmic;

							for(size_t i=0; i<pattern_groups; i++) {
								if (samples[i].name.empty() == false)
									channel_clickables[i].text = get_filename(samples[i].name).substr(0, 5);

							}
							menu_status = "file " + get_filename(fs_data.file) + " read";

							regenerate_pattern_grid(display_mode->w, display_mode->h, &pat_clickables[pattern_group]);

							reset_all_patterns(&pat_clickables, &pat_clickables_lock, samples, false);

							sound_pars.sounds.clear();
						}
						else {
							lck    .unlock();
							pat_lck.unlock();

							menu_status = "cannot read " + get_filename(fs_data.file);
							do_error_message(font, screen, display_mode, menu_status);
						}

						redraw = true;
					}

					fs_action = fs_none;
				}
			}
			else if (fs_action == fs_save) {
				if (fs_data.finished) {
					if (fs_data.file.empty() == false) {
						draw_please_wait(font, screen, display_mode);

						std::string file     = fs_data.file;
						size_t      file_len = file.size();
						if (file_len > 7 && file.substr(file_len - 7) != "." PROG_EXT)
							file += "." PROG_EXT;

						std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
						if (write_file(file, pat_clickables, samples, file_parameters))
							menu_status = "file " + get_filename(fs_data.file) + " written";
						else
							do_error_message(font, screen, display_mode, "cannot write " + get_filename(fs_data.file));

						redraw = true;
					}
					fs_action = fs_none;
				}
			}
			else if (fs_action == fs_load_sample) {
				if (fs_data.finished) {
					if (fs_data.file.empty() == false) {
						std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
						sample *const s = &samples[fs_action_sample_index];
						s->name = fs_data.file;
						auto *old_s_pointer = s->s;
						delete s->s;

                                		s->s = new sound_sample(sample_rate, s->name);
						if (s->s->begin() == false) {
							delete s->s;
							s->s = nullptr;
						}

						if (s->s) {
							bool is_stereo = s->s->get_n_channels() >= 2;
							s->s->add_mapping(0, 0, 1.0);
							s->s->add_mapping(is_stereo ? 1 : 0, 1, 1.0);

							menu_status = "file " + get_filename(fs_data.file) + " read";
							channel_clickables[fs_action_sample_index].text = get_filename(s->name).substr(0, 5);
						}
						else {
							menu_status = "file " + get_filename(fs_data.file) + " NOT FOUND";
							do_error_message(font, screen, display_mode, get_filename(fs_data.file) + " invalid/not found");
						}

						for(size_t i=0; i<sound_pars.sounds.size(); i++) {
							if (sound_pars.sounds[i].s == old_s_pointer)
								sound_pars.sounds[i].s = s->s;
						}

						if (s->s)
							reset_pattern(&pat_clickables, fs_action_sample_index, s->s, false);

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
					else {
						lck.unlock();
						menu_status = "cannot create " + fs_data.file;
						do_error_message(font, screen, display_mode, menu_status);
					}

					fs_action = fs_none;
					redraw    = true;
				}
			}

			if (fs_data.finished && fs_data.file.empty() == false) {
				auto slash = fs_data.file.find_last_of('/');
				if (slash != std::string::npos)
					work_path = fs_data.file.substr(0, slash);
			}
		}

		// redraw screen
		if (redraw && fs_action == fs_none) {
			SDL_SetRenderDrawColor(screen, 0, 0, 0, 255);
			SDL_RenderClear(screen);

			int font_height = display_mode->h / 2 / 100;

			draw_clickables(font, screen, menu_button_clickables, { }, { });

			if (mode == m_pattern) {
				std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
				draw_clickables(font, screen, pat_clickables[pattern_group].pattern, pat_clickable_selected, pat_index, pat_clickables[pattern_group].dim);
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
				if (selected_midi_channel.has_value()) {
					draw_text(font, screen, midi_ch_widget.x, midi_ch_widget.y,  std::to_string(selected_midi_channel.value() + 1),
						{ { midi_ch_widget.text_w, midi_ch_widget.text_h } });
				}
				if (lp_filter_f.has_value())
					draw_text(font, screen, lp_filter_widget.x, lp_filter_widget.y, std::to_string(int(lp_filter_f.value())), { { lp_filter_widget.text_w, lp_filter_widget.text_h } });
				if (hp_filter_f.has_value())
					draw_text(font, screen, hp_filter_widget.x, hp_filter_widget.y, std::to_string(int(hp_filter_f.value())), { { hp_filter_widget.text_w, hp_filter_widget.text_h } });
				draw_text(font, screen, sound_saturation_widget.x, sound_saturation_widget.y, std::to_string(sound_saturation), { { sound_saturation_widget.text_w, sound_saturation_widget.text_h } });
				draw_text(font, screen, swing_widget.x, swing_widget.y, std::to_string(swing_amount), { { swing_widget.text_w, swing_widget.text_h } });
			}
			else if (mode == m_sample) {
				std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
				int                 vol_left  = 0;
				int                 vol_right = 0;
				bool                is_stereo = false;
				sound_sample *const s         = samples[fs_action_sample_index].s;
				auto                midi_note = samples[fs_action_sample_index].midi_note;
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
				draw_text(font, screen, n_steps_pars.x, n_steps_pars.y, std::to_string(pat_clickables[fs_action_sample_index].dim),
					{ { n_steps_pars.text_w, n_steps_pars.text_h } });
				draw_text(font, screen, pitch_pars.x, pitch_pars.y, std::to_string(s ? s->get_pitch_bend() : 0),
					{ { pitch_pars.text_w, pitch_pars.text_h } });
			}
			else {
				fprintf(stderr, "Internal error: %d\n", mode);
				break;
			}

			SDL_RenderPresent(screen);
			redraw = false;
		}

		SDL_Delay(1);

		// process mouse clicks etc
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
							pat_clickable_selected = find_clickable(pat_clickables[pattern_group].pattern, event.button.x, event.button.y);
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
							draw_please_wait(font, screen, display_mode);

							{
								std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
								write_file(path + "/before_clear." PROG_EXT, pat_clickables, samples, file_parameters);
							}
							{
								std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
								sound_pars.sounds.clear();
							}
							{
								std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
								for(size_t i=0; i<pattern_groups; i++) {
									for(auto & element: pat_clickables[i].pattern) {
										element.selected = false;
										element.text.clear();
									}

									for(auto & element: pat_clickables[i].note_delta)
										element = 0;

									{
										std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
										sample & s = samples[i];
										delete s.s;
										s.s = nullptr;
										s.name.clear();
									}
									channel_clickables[i].text.clear();
								}
							}

							redraw      = true;
							menu_status = "cleared";
						}
						else if (idx == pattern_load_idx) {
							fs_data.finished = false;
							fs_action = fs_load;
							SDL_ShowOpenFileDialog(fs_callback, &fs_data, win, sf_filters, 1, work_path.c_str(), false);
						}
						else if (idx == save_idx) {
							fs_data.finished = false;
							fs_action = fs_save;
							SDL_ShowSaveFileDialog(fs_callback, &fs_data, win, sf_filters, 1, work_path.c_str());
						}
						else if (idx == quit_idx) {
							do_exit = true;
						}
						else if (set_up_down_value(idx, swing_widget, 0, 200, &swing_amount, shift)) {
							swing_amount_parameter = swing_amount;
						}
						else if (set_up_down_value(idx, bpm_widget, 1, 999, &bpm, shift)) {
						}
						else if (set_up_down_value(idx, vol_widget, 0, 110, &vol, shift)) {  // this one goes to 11!
						}
						else if (set_up_down_value(idx, sound_saturation_widget, 0, 1000, &sound_saturation, shift)) {
							std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
							sound_pars.sound_saturation = 1. - sound_saturation / 1000.;
						}
						else if (configure_filter(&sound_pars, lp_filter_widget, idx, false, &lp_filter_f, shift)) {
							// taken
						}
						else if (configure_filter(&sound_pars, hp_filter_widget, idx, false, &hp_filter_f, shift)) {
							// taken
						}
						else if (set_up_down_value(idx, midi_ch_widget, 0, 15, &selected_midi_channel, shift)) {
							// taken
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
								SDL_ShowSaveFileDialog(fs_callback, &fs_data, win, sf_filters_record, 1, work_path.c_str());
							}
						}
						else if (idx == pause_idx) {
							paused = !paused;
							menu_buttons_clickables[pause_idx].selected = paused;
						}
						else if (idx == polyrythmic_idx) {
							polyrythmic = !polyrythmic;
							menu_buttons_clickables[polyrythmic_idx].selected = polyrythmic;
						}
						sleep_ms                 = 60 * 1000 / bpm;
						std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
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
							SDL_ShowOpenFileDialog(fs_callback, &fs_data, win, sf_filters_sample, 1, work_path.c_str(), false);
						}
						else if (idx == sample_unload_idx) {
							std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
							sample & s = samples[fs_action_sample_index];
							// menubar text
							channel_clickables[fs_action_sample_index].text.clear();
							// remove from queue
							for(size_t i=0; i<sound_pars.sounds.size();) {
								if (sound_pars.sounds[i].s == s.s)
									sound_pars.sounds.erase(sound_pars.sounds.begin() + i);
								else
									i++;
							}
							// delete sample from pattern
							delete s.s;
							s.s = nullptr;
							s.name.clear();
						}
						else if (idx == mute_idx) {
							std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
							sound_sample *const s = samples[fs_action_sample_index].s;
							bool new_state = !s->get_mute();
							s->set_mute(new_state);
							sample_buttons_clickables[mute_idx].selected = new_state;
						}
						else {
							std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
							sound_sample *const s         = samples[fs_action_sample_index].s;
							auto              & midi_note = samples[fs_action_sample_index].midi_note;
							bool                is_stereo = s ? s->get_n_channels() >= 2 : false;
							int                 pitch     = s ? s->get_pitch_bend() * 1000 : 0;

							if (set_up_down_value(idx, midi_note_widget_pars, 0, 127, &midi_note, shift)) {
								// taken
							}
							else if (set_up_down_value(idx, pitch_pars, 0, 10000, &pitch, shift)) {
								if (s)
									s->set_pitch_bend(pitch / 1000.);
							}
							else if (idx == n_steps_pars.up) {
								pat_clickables[fs_action_sample_index].dim = std::min(max_pattern_dim, pat_clickables[fs_action_sample_index].dim + 1);
								regenerate_pattern_grid(display_mode->w, display_mode->h, &pat_clickables[fs_action_sample_index]);
							}
							else if (idx == n_steps_pars.down) {
								pat_clickables[fs_action_sample_index].dim = std::max(size_t(2), pat_clickables[fs_action_sample_index].dim - 1);
								regenerate_pattern_grid(display_mode->w, display_mode->h, &pat_clickables[fs_action_sample_index]);
							}
							else if (s == nullptr) {
								// skip volume when no sample
							}
							else if (configure_volume(&sound_pars, sample_vol_widget_left, idx, s, 0, shift)) {
							}
							else if (is_stereo && configure_volume(&sound_pars, sample_vol_widget_right, idx, s, 1, shift)) {
							}
							else if (!is_stereo) {
								s->set_mapping_target_volume(1, s->get_mapping_target_volume(0));
							}
						}
					}
				}
				redraw = true;
			}
			else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
				std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
				if (pat_clickable_selected.has_value()) {
					pat_clickables[pattern_group].pattern[pat_clickable_selected.value()].selected = !pat_clickables[pattern_group].pattern[pat_clickable_selected.value()].selected;
					pat_clickable_selected.reset();
					redraw = true;
				}
			}
			else if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.scancode == SDL_SCANCODE_SPACE) {
					std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
					pat_clickables[pattern_group].pattern[pat_index].selected = !pat_clickables[pattern_group].pattern[pat_index].selected;
					redraw        = true;
					force_trigger = true;
				}
				else if (event.key.scancode == SDL_SCANCODE_LSHIFT || event.key.scancode == SDL_SCANCODE_RSHIFT) {
					shift = true;
				}
				else if (event.key.scancode == SDL_SCANCODE_UP || event.key.scancode == SDL_SCANCODE_DOWN) {
					std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
					auto & pattern   = pat_clickables[pattern_group];
					float  mouse_x   = -1;
					float  mouse_y   = -1;
					SDL_GetMouseState(&mouse_x, &mouse_y);
					int    i_mouse_x = mouse_x;
					int    i_mouse_y = mouse_y;
					auto   idx       = find_clickable(pat_clickables[pattern_group].pattern, i_mouse_x, i_mouse_y);
					if (idx.has_value()) {
						int    change    = shift ? 12 : 1;
						double direction = event.key.scancode == SDL_SCANCODE_UP ? change : -change;
						pattern.note_delta[idx.value()] += direction;

						std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
						sound_sample *const s = samples[pattern_group].s;
						if (s)
							pattern.pattern[idx.value()].text = midi_note_to_name(s->get_base_midi_note() + pattern.note_delta[idx.value()]);
						redraw = true;
					}
				}
			}
			else if (event.type == SDL_EVENT_KEY_UP) {
				if (event.key.scancode == SDL_SCANCODE_LSHIFT || event.key.scancode == SDL_SCANCODE_RSHIFT) {
					shift = false;
				}
			}
			else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
				auto & pattern = pat_clickables[pattern_group];
				auto   idx     = find_clickable(pat_clickables[pattern_group].pattern, event.wheel.mouse_x, event.wheel.mouse_y);
				if (idx.has_value()) {
					constexpr const int big_change   = 12;
					constexpr const int small_change = 1;
					double direction = 0;
					if (event.wheel.y < 0)
						direction = shift ? -big_change : -small_change;
					else if (event.wheel.y > 0)
						direction = shift ?  big_change :  small_change;

					pattern.note_delta[idx.value()] += direction;

					std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
					sound_sample *const s = samples[pattern_group].s;
					if (s)
						pattern.pattern[idx.value()].text = midi_note_to_name(s->get_base_midi_note() + pattern.note_delta[idx.value()]);

					redraw = true;
				}
			}
		}
	}

	draw_please_wait(font, screen, display_mode);

	player_thread.join();

	pw_main_loop_quit(sound_pars.pw.loop);
	sound_pars.pw.th->join();
	delete sound_pars.pw.th;

	{  // stop any recording
		std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
		if (sound_pars.record_handle)
			sf_close(sound_pars.record_handle);
	}

	{
		std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
		write_file(path + "/default." PROG_EXT, pat_clickables, samples, file_parameters);
	}

	unload_sample_cache();

	SDL_Quit();
	deinit_fonts();

	pw_deinit();

	return 0;
}
