#include "config.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <csignal>
#include <ctime>
#include <fstream>
#include <mutex>
#include <optional>
#if HAVE_SMF == 1
#include <smf.h>
#endif
#include <sndfile.h>
#include <unistd.h>
#include <vector>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "clickable.h"
#include "font.h"
#include "frequencies.h"
#include "gui.h"
#include "io.h"
#include "midi.h"
#include "midi-handler.h"
#include "mixer.h"
#include "player.h"
#include "sample.h"
#include "sdl3-audio.h"
#include "sf2.h"
#include "sound.h"
#include "time.h"
#include "utils.h"


using json = nlohmann::json;

std::string      cfg_file;
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
	if (filelist && filelist[0])
		fs_data->file = filelist[0];
	else
		fs_data->file.clear();
	fs_data->finished = true;
}

bool start_mid_recording(sound_parameters *const sound_pars, const std::string & file)
{
#if HAVE_SMF == 1
	std::unique_lock<std::mutex> lck(sound_pars->smf_lock);
	sound_pars->smf                  = smf_new();
	sound_pars->smf_track            = smf_track_new();
	sound_pars->smf_start            = get_us();
	sound_pars->smf_file_name        = file;
	smf_add_track(sound_pars->smf, sound_pars->smf_track);
	sound_pars->record_wav_smf_since = get_us();
#endif
	return true;
}

bool close_mid_file(sound_parameters *const sound_pars)
{
#if HAVE_SMF == 1
	auto rc = smf_save(sound_pars->smf, sound_pars->smf_file_name.c_str());
	smf_delete(sound_pars->smf);
	sound_pars->smf       = nullptr;
	sound_pars->smf_track = nullptr;
	return rc == 0;
#else
	return true;
#endif
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

std::optional<size_t> find_clickable(const std::vector<clickable> & clickables, const SDL_Event & event)
{
	for(size_t i=0; i<clickables.size(); i++) {
		if (clickables.at(i).is_triggered(event))
			return i;
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
		clickables.emplace_back(clickable({ x, y, channel_width, channel_height }, "", false, '1' + i));
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
	clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "menu", false, 'm'));

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
	clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height / 3 }, name, false, 0));

	y += menu_button_height / 3;
	pars->up = clickables.size() + click_offset;
	clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height / 3 }, "↑", false, 0));

	y += menu_button_height / 3;
	if (step10) {
		pars->up_10 = clickables.size() + click_offset;
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height / 3 }, "↑↑↑", false, 0));
	}
	else {
		pars->up_10 = size_t(-1);
	}
	y += menu_button_height / 3;
	pars->x  = x;
	pars->y  = y;
	y += menu_button_height / 3;
	if (step10) {
		pars->down_10 = clickables.size() + click_offset;
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height / 3 }, "↓↓↓", false, 0));
	}
	else {
		pars->down_10 = size_t(-1);
	}
	y += menu_button_height / 3;
	pars->down = clickables.size() + click_offset;
	clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height / 3 }, "↓", false, 0));
	y += menu_button_height / 3;

	return clickables;
}

std::vector<clickable> generate_cell_settings_menu_buttons(const int w, const int h, up_down_widget *const pitch_pars, up_down_widget *const volume_left_pars, up_down_widget *const volume_right_pars)
{
	int menu_button_width  = w * 15 / 100;

	std::vector<clickable> clickables;

	std::vector<clickable> pitch_widget = generate_up_down_widget(w, h, menu_button_width * 0, 0, "pitch", clickables.size(), pitch_pars);
	std::copy(pitch_widget.begin(), pitch_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> volume_left_widget = generate_up_down_widget(w, h, menu_button_width * 1, 0, "vol l", clickables.size(), volume_left_pars);
	std::copy(volume_left_widget.begin(), volume_left_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> volume_right_widget = generate_up_down_widget(w, h, menu_button_width * 2, 0, "vol r", clickables.size(), volume_right_pars);
	std::copy(volume_right_widget.begin(), volume_right_widget.end(), std::back_inserter(clickables));

	return clickables;
}

std::vector<clickable> generate_settings_menu_buttons(const int w, const int h, size_t *const pattern_load_idx, size_t *const save_idx,
		size_t *const clear_idx, size_t *const quit_idx, up_down_widget *const bpm_widget_pars, size_t *const record_idx,
		up_down_widget *const volume_widget_pars, size_t *const pause_idx, size_t *const midi_idx,
		up_down_widget *const sound_saturation_pars, size_t *const polyrythmic_idx,
		up_down_widget *const humanize_widget_pars, size_t *const agc_idx, size_t *const clipping_idx, size_t *const scope_idx,
		size_t *const busyness_idx, size_t *const record_time_idx, size_t *const scope_stereo_idx)
{
	int menu_button_width  = w * 15 / 100;
	int menu_button_height = h * 15 / 100;

	std::vector<clickable> clickables;

	int x = 0;
	int y = 0;
	{
		*pause_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "pause", false, 'p'));
		x += menu_button_width;
	}
	{
		*pattern_load_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "load", false, 'l'));
		x += menu_button_width;
	}
	{
		*record_idx  = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "record", false, 'r'));
		x += menu_button_width;
	}
	{
		*save_idx  = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "save", false, 's'));
		x += menu_button_width;
	}
	{
		*clear_idx  = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "clear", false, 'c'));
		x += menu_button_width;
	}
	int quit_x = x;
	{
		*quit_idx  = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "quit", false, 'q'));
		x += menu_button_width;
	}

	std::vector<clickable> bpm_widget = generate_up_down_widget(w, h, 0, y, "BPM", clickables.size(), bpm_widget_pars);
	std::copy(bpm_widget.begin(), bpm_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> volume_widget = generate_up_down_widget(w, h, menu_button_width, y, "volume", clickables.size(), volume_widget_pars);
	std::copy(volume_widget.begin(), volume_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> sound_saturation_pars_widget = generate_up_down_widget(w, h, menu_button_width * 2, y, "saturation", clickables.size(), sound_saturation_pars);
	std::copy(sound_saturation_pars_widget.begin(), sound_saturation_pars_widget.end(), std::back_inserter(clickables));

	int up_down_height = menu_button_height / 3 * 6;
	x = 0;
	y += up_down_height;

	std::vector<clickable> humanize_widget = generate_up_down_widget(w, h, x, y, "humanize", clickables.size(), humanize_widget_pars);
	std::copy(humanize_widget.begin(), humanize_widget.end(), std::back_inserter(clickables));
	x += menu_button_width;

	y += menu_button_height + up_down_height;

	x = 0;
	{
		*polyrythmic_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "polyryth.", false, 'P'));
		x += menu_button_width;
	}

	{
		*agc_idx         = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "AGC", false, 'A'));
		x += menu_button_width;
	}

	{
		*midi_idx         = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "MIDI", false, 'M'));
		x += menu_button_width;
	}

	{
		*scope_stereo_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "scope m.ch.", false, '_'));
		x += menu_button_width;
	}

	int half_height = menu_button_height / 2;
	x = quit_x;
	{
		int temp_y = menu_button_height;
		clickables.emplace_back(clickable({ x, temp_y, menu_button_width, half_height }, "clipping", false, 0));
		temp_y += half_height;
		*clipping_idx = clickables.size();
		clickables.emplace_back(clickable({ x, temp_y, menu_button_width, half_height }, "0%", false, 0));
		y += menu_button_height;
	}
	{
		int temp_y = menu_button_height * 2;
		clickables.emplace_back(clickable({ x, temp_y, menu_button_width, half_height }, "busyness", false, 0));
		temp_y += half_height;
		*busyness_idx = clickables.size();
		clickables.emplace_back(clickable({ x, temp_y, menu_button_width, half_height }, "0%", false, 0));
		x += menu_button_width;
		y += menu_button_height;
	}

	{
		*scope_idx = clickables.size();
		clickables.emplace_back(clickable({ int(menu_button_width * 4.1), 4 * menu_button_height, int(menu_button_width * 1.8), menu_button_height * 2 }, "", true, 0));
	}

	{
		*record_time_idx = clickables.size();
		clickables.emplace_back(clickable({ int(menu_button_width * 4.1), 6 * menu_button_height, int(menu_button_width * 1.8), half_height }, "", true, 0));
	}

	return clickables;
}

std::vector<clickable> generate_channel_buttons(const int w, const int h,
		size_t *const sample_load_idx, up_down_widget *const vol_widget_left_pars, up_down_widget *const vol_widget_right_pars,
		up_down_widget *const midi_note_widget_pars, up_down_widget *const n_steps_pars, up_down_widget *const pitch_pars,
		size_t *const sample_unload_idx, size_t *const mute_idx, up_down_widget *const echo_t_pars,
		up_down_widget *const lp_filter_pars, up_down_widget *const hp_filter_pars, size_t *const serial_notes_idx,
		up_down_widget *const swing_factor_pars, up_down_widget *const delay_factor_pars)
{
	int menu_button_width  = w * 15 / 100;
	int menu_button_height = h * 15 / 100;

	std::vector<clickable> clickables;

	int x = 0;
	int y = 0;
	{
		*sample_load_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "load", false, 'l'));
		x += menu_button_width;
	}
	{
		*sample_unload_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "unload", false, 'u'));
		x += menu_button_width;
	}
	x += menu_button_width;
	x += menu_button_width;
	{
		*mute_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "mute", false, 'M'));
		x += menu_button_width;
	}
	{
		*serial_notes_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "serial", false, 's'));
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

	std::vector<clickable> echo_t_pars_widget = generate_up_down_widget(w, h, menu_button_width * 5, y, "echo", clickables.size(), echo_t_pars);
	std::copy(echo_t_pars_widget.begin(), echo_t_pars_widget.end(), std::back_inserter(clickables));

	int up_down_height = menu_button_height / 3 * 6;
	y += up_down_height;

	std::vector<clickable> lp_filter_pars_widget = generate_up_down_widget(w, h, menu_button_width * 0, y, "low pass", clickables.size(), lp_filter_pars);
	std::copy(lp_filter_pars_widget.begin(), lp_filter_pars_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> hp_filter_pars_widget = generate_up_down_widget(w, h, menu_button_width * 1, y, "high pass", clickables.size(), hp_filter_pars);
	std::copy(hp_filter_pars_widget.begin(), hp_filter_pars_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> swing_pars_widget = generate_up_down_widget(w, h, menu_button_width * 2, y, "swing", clickables.size(), swing_factor_pars);
	std::copy(swing_pars_widget.begin(), swing_pars_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> delay_widget = generate_up_down_widget(w, h, menu_button_width * 3, y, "delay", clickables.size(), delay_factor_pars);
	std::copy(delay_widget.begin(), delay_widget.end(), std::back_inserter(clickables));

	return clickables;
}

std::vector<clickable> generate_midi_menu(const int w, const int h, size_t *const load_midi_sample_idx, up_down_widget *const midi_ch_widget_pars, up_down_widget *const volume_widget_left_pars, up_down_widget *const volume_widget_right_pars)
{
	int menu_button_width  = w * 15 / 100;
	int menu_button_height = h * 15 / 100;

	std::vector<clickable> clickables;

	int x = 0;
	int y = 0;
	{
		clickable c { };
		c.where               = { x, y, menu_button_width, menu_button_height };
		c.text                = "load";
		*load_midi_sample_idx = clickables.size();
		clickables.push_back(c);
		x += menu_button_width;
	}

	std::vector<clickable> midi_ch_widget = generate_up_down_widget(w, h, 0, y, "midi ch.", clickables.size(), midi_ch_widget_pars);
	std::copy(midi_ch_widget.begin(), midi_ch_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> volume_left_widget = generate_up_down_widget(w, h, menu_button_width, y, "vol l", clickables.size(), volume_widget_left_pars);
	std::copy(volume_left_widget.begin(), volume_left_widget.end(), std::back_inserter(clickables));

	std::vector<clickable> volume_right_widget = generate_up_down_widget(w, h, menu_button_width * 2, y, "vol r", clickables.size(), volume_widget_right_pars);
	std::copy(volume_right_widget.begin(), volume_right_widget.end(), std::back_inserter(clickables));

	return clickables;
}

void generate_pattern_grid(const int w, const int h, const int steps, pattern *const p)
{
	int pattern_w   = w * 85 / 100;
	int pattern_h   = h * 80 / 100;
	int offset_h    = h * 5 / 100;

	int steps_sq    = ceil(sqrt(steps));
	int step_width  = pattern_w / steps_sq;
	int step_height = pattern_h / steps_sq;

	p->pattern   .resize(max_pattern_dim);
	p->note_delta.resize(max_pattern_dim);
	p->dim  = steps;
	p->wdim = steps_sq;
	p->hdim = ceil(steps / double(steps_sq));

	for(size_t i=0; i<max_pattern_dim; i++) {
		p->volume_left .push_back(1.);
		p->volume_right.push_back(1.);
	}

	for(int i=0; i<steps; i++) {
		int x = (i % steps_sq) * step_width;
		int y = (i / steps_sq) * step_height + offset_h;
		p->pattern.at(i) = clickable({ x, y, step_width, step_height }, "", false, 0);
		p->note_delta.at(i) = 0;
	}
}

std::vector<clickable> generate_pattern_menu(const int w, const int h, size_t *const pause_idx, size_t *const restart_idx)
{
	int menu_button_width  = w * 15 / 100;
	int menu_button_height = h * 15 / 100;
	int x                  = 0;
	int y                  = h * 85 / 100;

	std::vector<clickable> clickables;

	{
		*pause_idx   = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "pause", false, 'p'));
		x += menu_button_width;
	}
	{
		*restart_idx = clickables.size();
		clickables.emplace_back(clickable({ x, y, menu_button_width, menu_button_height }, "rewind", false, 'r'));
		x += menu_button_width;
	}

	return clickables;
}

void regenerate_pattern_grid(const int w, const int h, pattern *const p)
{
	int pattern_w   = w * 85 / 100;
	int pattern_h   = h * 80 / 100;
	int offset_h    = h * 5 / 100;

	int steps_sq    = ceil(sqrt(p->dim));
	int step_width  = pattern_w / steps_sq;
	int step_height = pattern_h / steps_sq;

	for(size_t i=0; i<p->dim; i++) {
		int x = (i % steps_sq) * step_width;
		int y = (i / steps_sq) * step_height + offset_h;
		p->pattern.at(i).where = { x, y, step_width, step_height };
	}
}

void draw_scope(SDL_Renderer *const screen, const SDL_Rect & where, const std::vector<float> & scope, const bool is_left)
{
	if (scope.empty())
		return;

	if (is_left)
		SDL_SetRenderDrawColor(screen, 40, 255, 40, 255);
	else
		SDL_SetRenderDrawColor(screen, 255, 40, 40, 255);

	float px         = where.x;
	float py         = where.y + where.h * scope[0] / 2 + where.h / 2;
	for(size_t i=1; i<scope.size(); i++) {
		float x = where.x + i * float(where.w) / scope.size();
		float y = where.y + where.h * scope[i] / 2 + where.h / 2;
		SDL_RenderLine(screen, px, py, x, y);
		px = x;
		py = y;
	}
}

// hl_index: high light index
void draw_clickables(TTF_Font *const font_big, TTF_Font *const font_small, SDL_Renderer *const screen, const std::vector<clickable> & clickables,
		const std::optional<std::pair<size_t, uint64_t> > & hl_index, const std::optional<size_t> play_index, const ssize_t draw_limit = -1,
		const std::optional<size_t> & cursor = { })
{
	size_t   draw_n        = draw_limit == -1 ? clickables.size() : draw_limit;
	uint64_t now           = get_ms();
	bool     is_long_press = hl_index.has_value() ? now - hl_index.value().second > long_press_dt : false;

	for(size_t i=0; i<draw_n; i++) {
		bool hl    = hl_index  .has_value() == true && hl_index  .value().first == i;
		bool pl    = play_index.has_value() == true && play_index.value()       == i;
		int  extra = is_long_press && hl ? 55 : 0;
		std::vector<int> color;
		if (clickables[i].selected) {
			int sub_color = (hl ? 200 : 40) + extra;
			if (pl)
				color = { 200, 40, sub_color };
			else
				color = { 40, 200, sub_color };
		}
		else {
			int sub_color = (hl ? 100 : 40) + extra;
			if (pl)
				color = { 100, 40, sub_color };
			else
				color = { 40, 100, sub_color };
		}
		clickables[i].draw(font_big, font_small, screen, color);
	}

	if (cursor.has_value()) {
		const SDL_Rect & where = clickables[cursor.value()].where;
		SDL_FRect        r     = { float(where.x + where.w / 4.), float(where.y + where.h / 4.), float(where.w / 2.), float(where.h / 2.) };
                SDL_SetRenderDrawColor(screen, 0, 0, 255, 255);
                SDL_RenderRect(screen, &r);
	}
}

std::optional<size_t> select_from_list(TTF_Font *const font, TTF_Font *const font_small, SDL_Renderer *const screen, const int w, const int h, const unsigned font_height, const std::vector<std::pair<std::string, void *> > & list)
{
	if (list.empty())
		return { };

	int  dim_w              = w / 6;
	int  dim_h              = h / 6;
	int  menu_button_width  = w * 15 / 100;
	int  menu_button_height = h * 15 / 100;
	bool redraw             = true;
	int  border_w           = 0.05 * menu_button_width;
	int  border_h           = 0.05 * menu_button_height;

	std::vector<clickable> clickables;

	size_t button_ok = clickables.size();
	clickables.emplace_back(clickable({ int(dim_w + 0.05 * menu_button_width), int(h - dim_h - menu_button_height * 1.05), menu_button_width, menu_button_height }, "OK", false, 'o'));

	size_t button_cancel = clickables.size();
	clickables.emplace_back(clickable({ int(w - dim_w - menu_button_width * 1.05), int(h - dim_h - menu_button_height * 1.05), menu_button_width, menu_button_height }, "Cancel", false, 'c'));

	size_t button_up = clickables.size();
	clickables.emplace_back(clickable({ int(w / 2 - menu_button_width / 2), dim_h + border_h, menu_button_width, menu_button_height / 3}, "↑", false, 'u'));

	size_t button_down = clickables.size();
	clickables.emplace_back(clickable({ int(w / 2 - menu_button_width / 2), dim_h * 5 - border_h - menu_button_height, menu_button_width, menu_button_height / 3}, "↓", false, 'd'));

	size_t n_rows = (h - dim_h * 2 - menu_button_height / 3 - menu_button_height * 1.05) / font_height;
	printf("rows shown: %zu\n", n_rows);

	const int item_base_x = dim_w + border_w;
	const int item_base_y = dim_h + border_h + menu_button_height / 3;
	const int item_w      = dim_w * 2 - border_w;
	const int item_h      = font_height;

	ssize_t list_offset = 0;
	ssize_t cur_n_rows  = n_rows;

	bool   shift        = false;

	while(!do_exit) {
		if (redraw) {
			redraw = false;

			SDL_SetRenderDrawColor(screen, 0, 0, 0, 255);
			SDL_RenderClear(screen);

			SDL_FRect rec { float(dim_w), float(dim_h), float(w - dim_w * 2), float(h - dim_h * 2) };
			SDL_SetRenderDrawColor(screen, 50, 40, 40, 255);
			SDL_RenderFillRect(screen, &rec);
			SDL_SetRenderDrawColor(screen, 40, 40, 40, 191);
			SDL_RenderRect(screen, &rec);

			cur_n_rows = std::min(n_rows, list.size() - list_offset);
			for(ssize_t i=0; i<cur_n_rows; i++)
				draw_text(font, screen, item_base_x, item_base_y + i * item_h, list.at(i + list_offset).first, { { item_w, item_h } }, i == 0, text_alignment::left, text_alignment::top);

			draw_clickables(font, font_small, screen, clickables, { }, { });

			SDL_RenderPresent(screen);
		}

		SDL_Event event { };
		if (SDL_WaitEvent(&event)) {
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_UP) {
				auto button_clicked = find_clickable(clickables, event);
				if (button_clicked.has_value()) {
					size_t idx = button_clicked.value();
					if (idx == button_ok)
						return list_offset;
					if (idx == button_cancel)
						return { };
					if (idx == button_up) {
						list_offset = std::max(ssize_t(0), ssize_t(shift ? list_offset - n_rows * 2 / 3 : list_offset - 1));
						redraw = true;
					}
					if (idx == button_down) {
						list_offset = std::min(list.size() - 1, shift ? list_offset + n_rows * 2 / 3 : list_offset + 1);
						redraw = true;
					}
				}
				else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
					if (event.button.x >= item_base_x && event.button.x < item_base_x + item_w &&
					    event.button.y >= item_base_y && event.button.y < item_base_y + cur_n_rows * item_h)
					{
						list_offset += (event.button.y - item_base_y) / item_h;
						redraw = true;
					}
				}
			}
			else if (event.key.scancode == SDL_SCANCODE_LSHIFT || event.key.scancode == SDL_SCANCODE_RSHIFT) {
				shift = event.type == SDL_EVENT_KEY_DOWN;
			}
		}
	}

	return { };
}

bool configure_filter(pattern *const pat, std::shared_mutex *const lock, const up_down_widget & widget, const size_t widget_idx, const bool is_highpass, const bool shift)
{
	int mul = shift ? 3 : 1;

	std::unique_lock<std::shared_mutex> lck(*lock);
	std::optional<double> & frequency = is_highpass ? pat->hp_cutoff : pat->lp_cutoff;

	if (widget_idx == widget.up) {
		if (frequency.has_value() == false)
			frequency = 1.;
		else {
			frequency = std::min(sample_rate / 2., frequency.value() + 20 * mul);
			if (frequency >= sample_rate / 2)
				frequency.reset();
		}
	}
	else if (widget_idx == widget.up_10) {
		if (frequency.has_value() == false)
			frequency = 1.;
		else {
			frequency = std::min(sample_rate / 2., frequency.value() + 1000 * mul);
			if (frequency >= sample_rate / 2)
				frequency.reset();
		}
	}
	else if (widget_idx == widget.down) {
		if (frequency.has_value() == false)
			frequency = sample_rate / 2.;
		else {
			frequency = std::max(0., frequency.value() - 20 * mul);
			if (frequency < 1.)
				frequency.reset();
		}
	}
	else if (widget_idx == widget.down_10) {
		if (frequency.has_value() == false)
			frequency = sample_rate / 2.;
		else {
			frequency = std::max(0., frequency.value() - 1000 * mul);
			if (frequency < 1.)
				frequency.reset();
		}
	}
	else {
		return false;
	}

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

bool set_up_down_value(const size_t idx, const up_down_widget & widget, double *const value, const bool shift)
{
	int mul = shift ? 3 : 1;

	if (idx == widget.up)
		(*value) = std::min(1., *value + 0.01 * mul);
	else if (idx == widget.up_10)
		(*value) = std::min(1., *value + 0.1  * mul);
	else if (idx == widget.down)
		(*value) = std::max(0., *value - 0.01 * mul);
	else if (idx == widget.down_10)
		(*value) = std::max(0., *value - 0.1  * mul);
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
		s->set_volume(channel_index, std::min(10., s->get_volume(channel_index) + 0.01 * mul));
	else if (widget_idx == widget.up_10)
		s->set_volume(channel_index, std::min(10., s->get_volume(channel_index) + 0.1 * mul));
	else if (widget_idx == widget.down)
		s->set_volume(channel_index, std::max(0., s->get_volume(channel_index) - 0.01 * mul));
	else if (widget_idx == widget.down_10)
		s->set_volume(channel_index, std::max(0., s->get_volume(channel_index) - 0.1 * mul));
	else
		return false;

	return true;
}

size_t get_cursor_index(const pattern & pattern)
{
	return pattern.cursor.value().second * pattern.wdim + pattern.cursor.value().first;
}

void reset_pattern(std::array<pattern, pattern_groups> *const pat_clickables, const size_t pattern_group, sound_sample *const s, const bool zero)
{
	auto & pattern = (*pat_clickables)[pattern_group];

	for(size_t i=0; i<pattern.pattern.size(); i++) {
		if (zero) {
			pattern.note_delta  [i] = 0.;
			pattern.volume_left [i] = 1.;
			pattern.volume_right[i] = 1.;
		}

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

void draw_message(TTF_Font *const font, SDL_Renderer *const screen, int win_width, int win_height, const std::string & message, const uint8_t r, const uint8_t g, const uint8_t b)
{
	int dim_w = win_width / 6;
	int dim_h = win_height / 6;

	SDL_FRect rec { float(dim_w), float(dim_h), float(win_width - dim_w * 2), float(win_height - dim_h * 2) };
	SDL_SetRenderDrawColor(screen, 50, 40, 40, 255);
	SDL_RenderFillRect(screen, &rec);
	SDL_SetRenderDrawColor(screen, 40, 40, 40, 191);
	SDL_RenderRect(screen, &rec);

	SDL_SetRenderDrawColor(screen, r, g, b, 255);
	draw_text(font, screen, 0, 0, message, { { win_width, win_height } }, true);
	SDL_RenderPresent(screen);
}

void draw_please_wait(TTF_Font *const font, SDL_Renderer *const screen, int win_width, int win_height)
{
	draw_message(font, screen, win_width, win_height, "Please wait", 40, 200, 40);
}

void wait_for_any_event()
{
	while(!do_exit) {
		SDL_Event event { };
		if (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_DOWN)
				break;
		}

		SDL_Delay(5);
	}
}

void do_error_message(TTF_Font *const font, SDL_Renderer *const screen, int win_width, int win_height, const std::string & error)
{
	draw_message(font, screen, win_width, win_height, error, 255, 40, 40);

	wait_for_any_event();

	SDL_SetRenderDrawColor(screen, 40, 60, 40, 255);
	SDL_RenderClear(screen);
	SDL_RenderPresent(screen);
}

bool are_you_sure(TTF_Font *const font_big, TTF_Font *const font_small, SDL_Renderer *const screen, int win_width, int win_height, const int font_height, const std::string & question, const std::string & question_2 = "Are you sure?")
{
	int dim_w              = win_width / 6;
	int dim_h              = win_height / 6;
	int scr_half_w         = win_width / 2;
	int scr_half_h         = win_height / 2;

	int menu_button_width  = win_width * 10 / 100;
	int menu_button_height = win_height * 10 / 100;

	SDL_FRect r { float(dim_w), float(dim_h), float(win_width - dim_w * 2), float(win_height - dim_h * 2) };
	SDL_SetRenderDrawColor(screen, 50, 40, 40, 255);
	SDL_RenderFillRect(screen, &r);
	SDL_SetRenderDrawColor(screen, 40, 40, 40, 191);
	SDL_RenderRect(screen, &r);

	int  x1                = scr_half_w - scr_half_w / 3;
	int  y1                = scr_half_h;
	int  x2                = scr_half_w + scr_half_w / 3;
	int  y2                = scr_half_h;

        std::vector<clickable> clickables;
	clickables.emplace_back(clickable({ x1 - menu_button_width / 2, y1, menu_button_width, menu_button_height }, "Yes", false, 'y'));
	clickables.emplace_back(clickable({ x2 - menu_button_width / 2, y2, menu_button_width, menu_button_height }, "No",  false, 'n'));

	draw_clickables(font_big, font_small, screen, clickables, { }, { });

	SDL_SetRenderDrawColor(screen, 255, 40, 40, 255);
	draw_text(font_big, screen, 0, scr_half_h - font_height * 2, question,   { { win_width, font_height } }, true);
	draw_text(font_big, screen, 0, scr_half_h - font_height,     question_2, { { win_width, font_height } }, true);
	SDL_RenderPresent(screen);

	while(!do_exit) {
		SDL_Event event { };
		if (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_UP) {
				auto button_clicked = find_clickable(clickables, event);
				if (button_clicked.has_value()) {
					SDL_SetRenderDrawColor(screen, 40, 60, 40, 255);
					SDL_RenderClear(screen);
					SDL_RenderPresent(screen);
					return button_clicked.value() == 0;
				}
			}
		}

		SDL_Delay(1);
	}

	return false;
}

bool start_wav_recording(sound_parameters *const sound_pars, const std::string & file, const std::array<sample, pattern_groups> & samples, const bool record_multichannel)
{
	// count number of channels: this is required as a sample can have 1 (mono), 2 (stereo) or
	// maybe even more channels
	sound_pars->record_ch_offsets.clear();
	int channel_count = 0;
	for(size_t i=0; i<pattern_groups; i++) {
		sound_pars->record_ch_offsets.push_back(channel_count);
		channel_count += samples[i].s ? samples[i].s->get_n_channels() : 0;
	}
	assert(sound_pars->record_ch_offsets.size() <= pattern_groups);
	printf("%d channels\n", channel_count);

	// init wav file
	SF_INFO si { };
	si.samplerate = sample_rate;
	si.channels   = record_multichannel ? channel_count : sound_pars->n_channels;
	si.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
	auto handle   = sf_open(file.c_str(), SFM_WRITE, &si);

	std::unique_lock<std::mutex> r_lck(sound_pars->record_lock);
	sound_pars->record_handle        = handle;
	sound_pars->record_wav_smf_since = get_us();
	sound_pars->record_n_channels    = channel_count;
	sound_pars->record_multichannel  = record_multichannel;

	return sound_pars->record_handle != nullptr;
}

void clear_everything(std::array<pattern, pattern_groups> & pat_clickables, std::shared_mutex *const pat_clickables_lock, sound_parameters & sound_pars,
		std::string *const menu_status, const std::string & path, std::array<sample, pattern_groups> & samples,
		const std::vector<file_parameter> & file_parameters, std::vector<clickable> & channel_clickables, std::string *const kaboem_file)
{
	{
		const std::string file_name = path + "/before_clear." PROG_EXT;

		std::shared_lock<std::shared_mutex> pat_lck(*pat_clickables_lock);
		if (write_file(file_name, pat_clickables, samples, file_parameters, &sound_pars.midi_sample) == false)
			menu_status->assign("failed: " + file_name);
	}
	{
		std::lock_guard<std::shared_mutex> pat_lck(*pat_clickables_lock);

		std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
		sound_pars.sounds.clear();

		for(size_t i=0; i<pattern_groups; i++) {
			for(auto & element: pat_clickables[i].pattern) {
				element.selected = false;
				element.text.clear();
			}

			for(auto & element: pat_clickables[i].note_delta)
				element = 0.;

			for(auto & element: pat_clickables[i].volume_left)
				element = 1.;
			for(auto & element: pat_clickables[i].volume_right)
				element = 1.;

			{
				sample & s = samples[i];
				delete s.s;
				s.s = nullptr;
				s.name.clear();
			}
			channel_clickables[i].text.clear();
		}
	}

	kaboem_file->clear();
}

void set_samples_buttons(sound_parameters & sound_pars, std::array<sample, pattern_groups> & samples, std::vector<clickable> & channel_buttons_clickables, const size_t idx, const size_t mute_idx, const size_t serial_note_idx, const bool serial_state)
{
	std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
	sound_sample *const s = samples[idx].s;
	if (s)
		channel_buttons_clickables[mute_idx].selected = s->get_mute();

	channel_buttons_clickables[serial_note_idx].selected = serial_state;
}

void set_bpm_sleep(std::atomic_int *const sleep_us, const int bpm)
{
	*sleep_us = 60 * 1000000 / (bpm * 4);
}

void update_queued_sounds(std::vector<sound_parameters::queued_sound> & sounds, const sample *const sample_)
{
	sound_sample *const s = sample_->s;
	for(auto & sound: sounds) {
		if (sound.s == s) {
			sound.volume_left  = s->get_volume(0);
			sound.volume_right = s->get_volume(1);
			sound.echo_t       = sample_->echo_t;
		}
	}
}

void load_configuration(std::string *const path, std::string *const kaboem_file)
{
	std::ifstream ifs(cfg_file);
	if (ifs.is_open() == false)
		return;
	ifs.exceptions(std::ifstream::badbit);

	json j = json::parse(ifs);

	if (j.contains("path"))
		path->assign(j["path"]);

	if (j.contains("kaboem-file"))
		kaboem_file->assign(j["kaboem-file"]);
}

void save_configuration(const std::string & path, const std::string & kaboem_file)
{
	json out;
	out["path"]        = path;
	out["kaboem-file"] = kaboem_file;

        try {
                std::ofstream o(cfg_file);
                o.exceptions(std::ifstream::badbit);
                o << out;
        }
        catch(const std::ifstream::failure& e) {
                printf("Cannot access \"%s\"\n", cfg_file.c_str());
        }
}

void set_max_scheduling_priority(std::thread & target)
{
	sched_param sched_parameters { };
	sched_parameters.sched_priority = sched_get_priority_max(SCHED_FIFO);
	int p_rc_fifo = pthread_setschedparam(target.native_handle(), SCHED_FIFO, &sched_parameters);
	if (p_rc_fifo == 0)
		return;  // all good

	int scheduling_policy { };
	pthread_getschedparam(target.native_handle(), &scheduling_policy, &sched_parameters);
	sched_parameters.sched_priority = sched_get_priority_max(scheduling_policy);
	int p_rc = pthread_setschedparam(target.native_handle(), scheduling_policy, &sched_parameters);
	if (p_rc != 0)
		printf("Failed to set scheduling parameters for thread: %s\n", strerror(errno));
}

std::optional<sample> chose_and_load_sf2_sample(TTF_Font *const font, TTF_Font *const font_small, SDL_Renderer *const screen, const int w, const int h, const unsigned font_height, sound_parameters *const sound_pars, const std::string & file_name)
{
	std::map<uint16_t, sample_set_t> sample_set = load_sf2(file_name, false);
	if (sample_set.empty()) {
		do_error_message(font, screen, w, h, "Invalid SF2 file");
		return { };
	}

	// create list of all samples in set
	std::vector<std::pair<std::string, void *> > sample_names;
	for(auto & it: sample_set) {
		for(auto & sample: it.second.samples)
			sample_names.push_back({ sample.file_name, &sample });
	}

	std::optional<size_t> chosen = select_from_list(font, font_small, screen, w, h, font_height, sample_names);
	if (chosen.has_value() == false)
		return { };

	std::string   chosen_name   = sample_names.at(chosen.value()).first;
	sf2_sample_t *chosen_sample = reinterpret_cast<sf2_sample_t *>(sample_names.at(chosen.value()).second);

	return convert_sf2_sample(chosen_sample);
}

std::string set_sample(TTF_Font *const font, SDL_Renderer *const screen, const int w, const int h, sound_parameters *const sound_pars, std::array<sample, pattern_groups> & samples, const size_t sample_index, std::array<pattern, pattern_groups> *const pat_clickables, std::vector<clickable> *const channel_clickables, sample & s_in)
{
	std::unique_lock<std::shared_mutex> lck(sound_pars->sounds_lock);
	sample *const s = &samples[sample_index];
	auto *old_s_pointer = s->s;
	delete s->s;

	*s = s_in;

	printf("%s %zu %zu| %zu\n", s->name.c_str(), s->s->get_n_channels(), s->s->get_sample_count(), sample_index);

	std::string menu_status = "file " + s->name + " read";

	channel_clickables->at(sample_index).text = s->name.substr(0, 5);
	s->s->set_volume(0, 1.);
	s->s->set_volume(1, 1.);

	for(size_t i=0; i<sound_pars->sounds.size(); i++) {
		if (sound_pars->sounds[i].s == old_s_pointer)
			sound_pars->sounds[i].s = s->s;
	}

	if (s->s)
		reset_pattern(pat_clickables, sample_index, s->s, false);

	return menu_status;
}

int main(int argc, char *argv[])
{
	bool             full_screen = true;
	std::atomic_bool paused      = true;

	auto pref_path = SDL_GetPrefPath("vanheusden", "Kaboem");
	if (pref_path)
		cfg_file = pref_path + std::string("settings.json");
	else {
		const char *home = getenv("HOME");
		if (home)
			cfg_file = home + std::string("/.kaboem.json");
		else
			cfg_file = ".kaboem.json";
	}

	printf("Using configuration file: %s\n", cfg_file.c_str());

	bool multi_channel_wav = false;
	int  c                 = -1;
	while((c = getopt(argc, argv, "wum")) != -1) {
		if (c == 'w')
			full_screen = false;
		else if (c == 'u')
			paused = false;
		else if (c == 'm')
			multi_channel_wav = true;
		else {
			fprintf(stderr, "\"-%c\" is not understood\n", c);
			return 1;
		}
	}

	if (init_midi() == false)
		return 1;

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

	sound_parameters sound_pars(sample_rate, 2, multi_channel_wav);
	sound_pars.global_volume    = 1.;

	srand(time(nullptr));

	const std::string path      = SDL_GetBasePath();
	std::string       work_path = path;
	auto              midi_in   = allocate_midi_input_port();
	std::string       kaboem_file;

	load_configuration(&work_path, &kaboem_file);

	signal(SIGTERM, sigh);

	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024"      );
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,         "1"         );
	SDL_SetHint(SDL_HINT_RENDER_VSYNC,               "1"         );
	SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER,        "1"         );
	SDL_SetHint(SDL_HINT_APP_ID,                     PROG_NAME   );
	SDL_SetHint(SDL_HINT_APP_NAME,                   PROG_NAME   );
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_NAME,   PROG_NAME   );
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_ICON_NAME, "audio-card");

	SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
	if (display_id == 0) {
		SDL_Log("Failed to get primary display: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_Window *win = nullptr;

	if (full_screen) {
		const SDL_DisplayMode *display_mode = SDL_GetCurrentDisplayMode(display_id);
		if (display_mode == nullptr) {
			SDL_Log("Failed to get display mode: %s", SDL_GetError());
			SDL_Quit();
			return 1;
		}
		win = SDL_CreateWindow(PROG_NAME,
				  display_mode->w, display_mode->h,
				  (full_screen ? SDL_WINDOW_FULLSCREEN: 0));
	}
	else {
		win = SDL_CreateWindow(PROG_NAME, 320, 240, SDL_WINDOW_RESIZABLE);
		SDL_MaximizeWindow(win);
		SDL_SyncWindow(win);
	}
	assert(win);

	int win_width  = 0;
	int win_height = 0;
	SDL_GetWindowSize(win, &win_width, &win_height);
	printf("Window size: %dx%d\n", win_width, win_height);

	SDL_Renderer *screen = SDL_CreateRenderer(win, nullptr);
	assert(screen);

	init_fonts();

	unsigned  font_height = win_height * 5 / 100;
	TTF_Font *font        = load_font_by_filenames({ "Arial.ttf", "FreeSans.ttf", "DejaVuSans.ttf" }, font_height, false);
	if (font == nullptr)
		font = load_font({ "Free Sans", "Arial", "Ubuntu Sans Regular", "DejaVu Sans" }, font_height, false);
	assert(font);
	TTF_Font *font_small  = load_font_by_filenames({ "Arial.ttf", "FreeSans.ttf", "DejaVuSans.ttf" }, font_height, false);
	if (font_small == nullptr)
		font_small = load_font({ "Free Sans", "Arial", "Ubuntu Sans Regular", "DejaVu Sans" }, font_height / 2, false);
	assert(font_small);

	bool redraw = true;
	int  steps  = 16;
	int  bpm    = 135;
	int  vol    = 100;

	enum { m_pattern, m_settings, m_sample, m_cell, m_midi } mode = m_pattern;
	enum { fs_load, fs_save, fs_none, fs_load_sample, fs_record, fs_load_midi_sample } fs_action = fs_none;
	size_t fs_action_sample_index        = 0;
	fileselector_data      fs_data { };
	std::shared_mutex      pat_clickables_lock;
	std::array<pattern, pattern_groups> pat_clickables { };
	std::optional<size_t>  pat_clickable_selected;
	uint64_t               pat_clickable_pressed_since = 0;
	size_t                 pattern_group = 0;

	std::vector<clickable> channel_clickables      = generate_channel_column(win_width, win_height, pattern_groups);

	std::vector<clickable> menu_button_clickables  = generate_menu_button(win_width, win_height);

	size_t         pattern_load_idx = 0;
	size_t         save_idx         = 0;
	size_t         clear_idx        = 0;
	size_t         quit_idx         = 0;
	up_down_widget bpm_widget         { };
	size_t         record_idx       = 0;
	size_t         pause_idx        = 0;
	up_down_widget vol_widget         { };
	size_t         midi_idx         = 0;
	up_down_widget sound_saturation_widget { };
	int            sound_saturation = 0;
	std::optional<int> selected_percussion_midi_channel;
	std::atomic_int midi_thread_selected_percussion_midi_channel = -1;
	size_t         polyrythmic_idx  = 0;
	std::atomic_bool polyrythmic    = false;
	up_down_widget humanize_widget    { };
	int            humanize_amount  = 0;
	size_t         clipping_idx     = 0;
	size_t         busyness_idx     = 0;
	size_t         agc_idx          = 0;
	bool           agc              = false;
	size_t         scope_idx        = 0;
	size_t         record_time_idx  = 0;
	size_t         scope_stereo_idx = 0;
	bool           scope_stereo     = false;
	std::vector<clickable> settings_menu_buttons = generate_settings_menu_buttons(win_width, win_height,
			&pattern_load_idx, &save_idx, &clear_idx, &quit_idx, &bpm_widget, &record_idx, &vol_widget,
			&pause_idx, &midi_idx, &sound_saturation_widget,
			&polyrythmic_idx, &humanize_widget, &agc_idx, &clipping_idx, &scope_idx, &busyness_idx,
			&record_time_idx, &scope_stereo_idx);
	std::string    menu_status;

	up_down_widget pitch_widget             { };
	up_down_widget cell_volume_left_widget  { };
	up_down_widget cell_volume_right_widget { };
	std::vector<clickable> cell_menu_buttons = generate_cell_settings_menu_buttons(win_width, win_height,
			&pitch_widget, &cell_volume_left_widget, &cell_volume_right_widget);

	size_t         sample_load_idx        = 0;
	size_t         sample_unload_idx      = 0;
	size_t         mute_idx               = 0;
	size_t         serial_notes_idx       = 0;
	up_down_widget sample_vol_widget_left   { };
	up_down_widget sample_vol_widget_right  { };
	up_down_widget midi_note_widget_pars    { };
	up_down_widget n_steps_pars             { };
	up_down_widget pitch_pars               { };
	up_down_widget echo_t_pars              { };
	up_down_widget lp_filter_widget         { };
	up_down_widget hp_filter_widget         { };
	up_down_widget swing_factor_widget      { };
	up_down_widget delay_factor_widget      { };
	std::vector<clickable> channel_buttons_clickables = generate_channel_buttons(win_width, win_height,
			&sample_load_idx, &sample_vol_widget_left, &sample_vol_widget_right, &midi_note_widget_pars,
			&n_steps_pars, &pitch_pars, &sample_unload_idx, &mute_idx, &echo_t_pars,
			&lp_filter_widget, &hp_filter_widget, &serial_notes_idx, &swing_factor_widget, &delay_factor_widget);

	size_t         load_midi_sample_idx   = 0;
	up_down_widget midi_ch_widget           { };
	up_down_widget midi_volume_left_widget  { };
	up_down_widget midi_volume_right_widget { };
	int            midi_volume_left       = 127;
	int            midi_volume_right      = 127;
	std::vector<clickable> midi_menu_buttons = generate_midi_menu(win_width, win_height, &load_midi_sample_idx, &midi_ch_widget, &midi_volume_left_widget, &midi_volume_right_widget);

	size_t         p_pause_idx            = 0;
	size_t         restart_idx            = 0;
	std::vector<clickable> pattern_menu = generate_pattern_menu(win_width, win_height, &p_pause_idx, &restart_idx);
	for(size_t i=0; i<pattern_groups; i++)
		generate_pattern_grid(win_width, win_height, steps, &pat_clickables[i]);

	std::array<sample, pattern_groups> samples { };

	SDL_DialogFileFilter sf_filters[]        { { "Kaboem files", PROG_EXT           } };
	SDL_DialogFileFilter sf_filters_sample[] { { "Samples",      "wav;mp3;flac;sf2" } };
	SDL_DialogFileFilter sf_filters_sf2[]    { { "Sound font",   "sf2"              } };
	SDL_DialogFileFilter sf_filters_record[] { { "Record",       "wav;mid"          } };

	const std::vector<file_parameter> file_parameters {
		{ "bpm",           file_parameter::T_INT,   &bpm,               nullptr,                           nullptr, nullptr, nullptr, nullptr      },
		{ "volume",        file_parameter::T_INT,   &vol,               nullptr,                           nullptr, nullptr, nullptr, nullptr      },
		{ "saturation",    file_parameter::T_INT,   &sound_saturation,  nullptr,                           nullptr, nullptr, nullptr, nullptr      },
		{ "midi-channel",  file_parameter::T_INT,   nullptr,            &selected_percussion_midi_channel, nullptr, nullptr, nullptr, nullptr      },
		{ "midi-volume-l", file_parameter::T_INT,   &midi_volume_left,  nullptr,                           nullptr, nullptr, nullptr, nullptr      },
		{ "midi-volume-r", file_parameter::T_INT,   &midi_volume_right, nullptr,                           nullptr, nullptr, nullptr, nullptr      },
		{ "humanize-factor", file_parameter::T_INT, &humanize_amount,   nullptr,                           nullptr, nullptr, nullptr, nullptr      },
		{ "polyrythmic",   file_parameter::T_ABOOL, nullptr,            nullptr,                           nullptr, nullptr, nullptr, &polyrythmic },
		{ "agc",           file_parameter::T_BOOL,  nullptr,            nullptr,                           nullptr, nullptr, &agc,    nullptr      }
	};

	std::atomic_int humanize_amount_parameter { humanize_amount };

	if (kaboem_file.empty() == false && read_file(kaboem_file, &pat_clickables, &samples, &file_parameters, &sound_pars.midi_sample)) {
		for(size_t i=0; i<pattern_groups; i++) {
			if (samples[i].name.empty() == false)
				channel_clickables[i].text = get_filename(samples[i].name).substr(0, 5);
		}

		sound_pars.global_volume                        = vol / 100.;
		sound_pars.sound_saturation                     = 1. - sound_saturation / 1000.;
		sound_pars.agc_enabled                          = agc;
		settings_menu_buttons[agc_idx].selected         = agc;
		settings_menu_buttons[polyrythmic_idx].selected = polyrythmic;
		humanize_amount_parameter                       = humanize_amount;

		if (selected_percussion_midi_channel.has_value())
			midi_thread_selected_percussion_midi_channel = selected_percussion_midi_channel.value();
		else
			midi_thread_selected_percussion_midi_channel = -1;

		regenerate_pattern_grid(win_width, win_height, &pat_clickables[pattern_group]);

		reset_all_patterns(&pat_clickables, &pat_clickables_lock, samples, false);
	}

	std::atomic_int      sleep_us       = 0;
	size_t               prev_pat_index = size_t(-1);
	std::atomic_bool     force_trigger  = false;
	bool                 shift          = false;
	bool                 ctrl           = false;
	int                  prev_scope_t   = -1;
	size_t               selected_cell  = 0;
	std::atomic_uint64_t start_t        = 0;

	set_bpm_sleep(&sleep_us, bpm);

	pattern_menu         [p_pause_idx].selected = paused;
	settings_menu_buttons[pause_idx]  .selected = paused;

	std::thread player_thread([&pat_clickables, &pat_clickables_lock, &samples, &sleep_us, &sound_pars, &paused, &force_trigger, &polyrythmic, &humanize_amount_parameter, &start_t] {
			set_thread_name("KAB-player");
			player(&pat_clickables, &pat_clickables_lock, &samples, &sleep_us, &sound_pars, &paused, &do_exit, &force_trigger, &polyrythmic, &humanize_amount_parameter, &start_t);
			});

	std::thread mixer_thread([&sound_pars] {
			set_thread_name("KAB-mixer");
			mixer(&do_exit, &sound_pars);
		});

	std::atomic_bool midi_triggered = false;
	std::thread midi_thread([&sound_pars, midi_in, &midi_thread_selected_percussion_midi_channel, &midi_triggered] {
			set_thread_name("KAB-MIDI");
			midi_processor(&sound_pars, midi_in, &midi_thread_selected_percussion_midi_channel, &midi_triggered, &do_exit);
	});

	set_max_scheduling_priority(mixer_thread);

	if (configure_sdl3_audio(&sound_pars) == false)
		return 1;

	while(!do_exit) {
		// determine pattern index
		size_t pat_index = 0;
		{
			if (paused)
				start_t = get_us();
			auto   now         = get_us() - start_t;
			std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
			size_t current_dim = pat_clickables[pattern_group].dim;

                        if (polyrythmic)
				pat_index = now / sleep_us % current_dim;
			else {
				size_t max_steps = 0;
                                for(size_t i=0; i<pattern_groups; i++) {
                                        if (samples[i].s != nullptr)
                                                max_steps = std::max(max_steps, pat_clickables[i].dim);
                                }
				pat_index = size_t(now / double(sleep_us) * current_dim / double(max_steps)) % current_dim;
                        }
		}
		if (pat_index != prev_pat_index && !paused) {
			redraw = true;
			prev_pat_index = pat_index;
		}

		// MIDI percussion events set a pattern-cell
		if (midi_triggered.exchange(false)) {
			double volume_l = midi_volume_left  / 127.;
			double volume_r = midi_volume_right / 127.;
			queue_sample(&sound_pars, 0, volume_l, volume_r, &sound_pars.midi_sample, &pat_clickables[pattern_group], pat_index, nullptr);

			std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
			if (!paused)
				pat_clickables[pattern_group].pattern[pat_index].selected = true;
			redraw = true;
		}

		// did the user select a file in the fileselector?
		if (fs_action != fs_none) {
			std::lock_guard<std::mutex> fs_lck(fs_data.lock);
			if (fs_action == fs_load) {
				if (fs_data.finished) {
					if (fs_data.file.empty() == false) {
						draw_please_wait(font, screen, win_width, win_height);

						clear_everything(pat_clickables, &pat_clickables_lock, sound_pars, &menu_status, work_path, samples,
								file_parameters, channel_clickables, &kaboem_file);

						std::unique_lock<std::shared_mutex> pat_lck (pat_clickables_lock        );
						std::unique_lock<std::shared_mutex> lck     (sound_pars.sounds_lock     );
						std::unique_lock<std::mutex       > midi_lck(sound_pars.midi_sample_lock);
						if (read_file(fs_data.file, &pat_clickables, &samples, &file_parameters, &sound_pars.midi_sample)) {
							kaboem_file                                     = fs_data.file;
							sound_pars.global_volume                        = vol / 100.;
							sound_pars.sound_saturation                     = 1. - sound_saturation / 1000.;
							sound_pars.agc_enabled                          = agc;
							settings_menu_buttons[agc_idx].selected         = agc;
							settings_menu_buttons[polyrythmic_idx].selected = polyrythmic;
							humanize_amount_parameter                       = humanize_amount;
							set_bpm_sleep(&sleep_us, bpm);

							for(size_t i=0; i<pattern_groups; i++) {
								if (samples[i].name.empty() == false)
									channel_clickables[i].text = get_filename(samples[i].name).substr(0, 5);

							}
							menu_status = "file " + get_filename(fs_data.file) + " read";

							regenerate_pattern_grid(win_width, win_height, &pat_clickables[pattern_group]);

							reset_all_patterns(&pat_clickables, &pat_clickables_lock, samples, false);

							sound_pars.sounds.clear();

							work_path = get_dirname(kaboem_file);
							save_configuration(work_path, kaboem_file);

							if (selected_percussion_midi_channel.has_value())
								midi_thread_selected_percussion_midi_channel = selected_percussion_midi_channel.value();
							else
								midi_thread_selected_percussion_midi_channel = -1;
						}
						else {
							lck    .unlock();
							pat_lck.unlock();

							menu_status = "cannot read " + get_filename(fs_data.file);
							do_error_message(font, screen, win_width, win_height, menu_status);
						}

						redraw = true;
					}

					fs_action = fs_none;
				}
			}
			else if (fs_action == fs_save) {
				if (fs_data.finished) {
					if (fs_data.file.empty() == false) {
						draw_please_wait(font, screen, win_width, win_height);

						std::string file     = fs_data.file;
						size_t      file_len = file.size();
						if (file_len > 7 && file.substr(file_len - 7) != "." PROG_EXT)
							file += "." PROG_EXT;

						kaboem_file = file;

						std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
						if (write_file(file, pat_clickables, samples, file_parameters, &sound_pars.midi_sample)) {
							menu_status = "file " + get_filename(fs_data.file) + " written";
							work_path   = get_dirname(kaboem_file);
							save_configuration(work_path, kaboem_file);
						}
						else {
							do_error_message(font, screen, win_width, win_height, "cannot write " + get_filename(fs_data.file));
						}

						redraw = true;
					}
					fs_action = fs_none;
				}
			}
			else if (fs_action == fs_load_sample) {
				if (fs_data.finished) {
					auto ext = fs_data.file.size() > 4 ? fs_data.file.substr(fs_data.file.size() - 4) : "";

					std::optional<sample> choice;
					if (ext == ".sf2")
						choice = chose_and_load_sf2_sample(font, font_small, screen, win_width, win_height, font_height, &sound_pars, fs_data.file);
					else {
						auto rc = load_sample(fs_data.file);
						if (rc.first.has_value()) {
							sample s { };
							s.name = get_filename(fs_data.file);
							s.s    = new sound_sample(sample_rate, s.name, rc.first.value().samples, rc.first.value().sample_rate);
							auto rc = s.s->begin();
							if (rc.has_value()) {
								delete s.s;
								s.s = nullptr;
								menu_status = rc.value();
							}
							else {
								choice = s;
							}
						}
						else {
							menu_status = rc.second;
						}
					}

					if (choice.has_value())
						menu_status = set_sample(font, screen, win_width, win_height, &sound_pars, samples, fs_action_sample_index, &pat_clickables, &channel_clickables, choice.value());

					redraw = true;
					fs_action = fs_none;
				}
			}
			else if (fs_action == fs_record) {
				if (fs_data.finished) {
					size_t name_len = fs_data.file.size();
					std::string ext = name_len >= 4 ? fs_data.file.substr(name_len - 4) : fs_data.file;

					bool succeeded = false;
					if (ext == ".wav") {
						bool multichannel = are_you_sure(font, font_small, screen, win_width, win_height, font_height, "Record to multichannel WAV-file?", "\"No\" produces a stereo file");
						succeeded = start_wav_recording(&sound_pars, fs_data.file, samples, multichannel);
					}
					else if (ext == ".mid") {
						succeeded = start_mid_recording(&sound_pars, fs_data.file);
					}

					if (succeeded)
						settings_menu_buttons[record_idx].selected = true;
					else {
						menu_status = "cannot create " + fs_data.file;
						do_error_message(font, screen, win_width, win_height, menu_status);
					}

					fs_action = fs_none;
					redraw    = true;
				}
			}
			else if (fs_action == fs_load_midi_sample) {
				if (fs_data.finished) {
					auto rc = chose_and_load_sf2_sample(font, font_small, screen, win_width, win_height, font_height, &sound_pars, fs_data.file);

					if (rc.has_value()) {
						std::unique_lock<std::mutex> lck(sound_pars.midi_sample_lock);
						delete sound_pars.midi_sample.s;
						sound_pars.midi_sample = rc.value();
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
		double current_clip_factor = 0.;
		int    busyness            = 0;
		if (mode == m_settings) {
			std::shared_lock<std::shared_mutex> lck(sound_pars.stats_lock);
			if (sound_pars.scope_t != prev_scope_t) {
				prev_scope_t = sound_pars.scope_t;
				redraw       = true;
			}
			current_clip_factor = sound_pars.clip_factor;
			busyness            = sound_pars.busyness;
		}

		if (sound_pars.record_wav_smf_since != 0 && mode == m_settings) {
			static uint64_t prev_record_wav_smf_since = 0;
			redraw |= sound_pars.record_wav_smf_since - prev_record_wav_smf_since >= 100000;  // update counter 10x/second
			prev_record_wav_smf_since = sound_pars.record_wav_smf_since;
		}

		if (redraw && fs_action == fs_none) {
			SDL_SetRenderDrawColor(screen, 0, 0, 0, 255);
			SDL_RenderClear(screen);

			int font_height = win_height * 2 / 100;

			draw_clickables(font, font_small, screen, menu_button_clickables, { }, { });

			if (mode == m_pattern) {
				std::optional<std::pair<size_t, uint64_t> > click_state;
				if (pat_clickable_selected.has_value())
					click_state = { pat_clickable_selected.value(), pat_clickable_pressed_since };

				draw_clickables(font, font_small, screen, pattern_menu, { }, { });

				std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
				if (pat_clickables[pattern_group].cursor.has_value()) {
					size_t cursor_idx = get_cursor_index(pat_clickables[pattern_group]);
					draw_clickables(font, font_small, screen, pat_clickables[pattern_group].pattern, click_state, pat_index, pat_clickables[pattern_group].dim, cursor_idx);
				}
				else {
					draw_clickables(font, font_small, screen, pat_clickables[pattern_group].pattern, click_state, pat_index, pat_clickables[pattern_group].dim);
				}
				draw_clickables(font, font_small, screen, channel_clickables, { }, pattern_group);

				int pattern_width = win_width * 85 / 100;
				int text_height   = win_height * 5 / 100;
				std::string sample_filename = get_filename(samples[pattern_group].name);
				if (sample_filename.empty() == false)
					draw_text(font, screen, 0, 0, sample_filename,
							{ { pattern_width, text_height } }, false, text_alignment::left,  text_alignment::top);
				if (kaboem_file.empty() == false)
					draw_text(font, screen, 0, 0, get_filename(kaboem_file),
							{ { pattern_width, text_height } }, false, text_alignment::right, text_alignment::top);
			}
			else if (mode == m_settings) {
				if (menu_status.empty())
					menu_status = PROG_NAME " " KABOEM_VERSION;
				draw_text(font, screen, 0, 0, menu_status, { { win_width, win_height } }, false, text_alignment::left, text_alignment::bottom);
				draw_clickables(font, font_small, screen, channel_clickables, { }, pattern_group);
				draw_clickables(font, font_small, screen, settings_menu_buttons, { }, { });
				draw_text(font, screen, bpm_widget.x, bpm_widget.y, std::to_string(bpm), { { bpm_widget.text_w, bpm_widget.text_h } });
				draw_text(font, screen, vol_widget.x, vol_widget.y, std::to_string(vol), { { vol_widget.text_w, vol_widget.text_h } });
				draw_text(font, screen, sound_saturation_widget.x, sound_saturation_widget.y, std::to_string(sound_saturation), { { sound_saturation_widget.text_w, sound_saturation_widget.text_h } });
				draw_text(font, screen, humanize_widget.x, humanize_widget.y, std::to_string(humanize_amount), { { humanize_widget.text_w, humanize_widget.text_h } });

				clickable & cc = settings_menu_buttons[clipping_idx];
				cc.text = std::to_string(int(ceil(current_clip_factor * 100))) + "%";
				draw_text(font, screen, cc.where.x, cc.where.y, cc.text, { { cc.where.w, cc.where.h } });

				clickable & cb = settings_menu_buttons[busyness_idx];
				cb.text = std::to_string(busyness) + "%";
				draw_text(font, screen, cb.where.x, cb.where.y, cb.text, { { cb.where.w, cb.where.h } });

				std::vector<float> scope_in;
				{
					std::shared_lock<std::shared_mutex> lck(sound_pars.stats_lock);
					scope_in = sound_pars.scope;
				}

				clickable & record_time_c = settings_menu_buttons[record_time_idx];
				clickable & scope_c       = settings_menu_buttons[scope_idx];
				const int   n_channels    = sound_pars.n_channels;
				if (scope_stereo == false) {
					std::vector<float>  scope;  // mono
					std::vector<double> avg_per_channel;
					scope.resize(scope_in.size() / n_channels);
					avg_per_channel.resize(n_channels);

					for(size_t i=0; i<scope_in.size(); i += n_channels) {
						size_t s_index = i / n_channels;
						for(int c=0; c<n_channels; c++) {
							scope[s_index]     += scope_in[i + c];
							avg_per_channel[c] += fabs(scope_in[i + c]);
						}
						scope[s_index] /= n_channels;
					}
					for(int c=0; c<n_channels; c++)
						avg_per_channel[c] /= scope_in.size() / n_channels;

					draw_scope(screen, scope_c.where, scope, true);

					for(int c=0; c<n_channels; c++) {
						int h_per_c = record_time_c.where.h / n_channels;
						SDL_SetRenderDrawColor(screen, 40 / (c * 2 + 1), 255, 40 / (c * 2 + 1), 255);
						for(int h=0; h<h_per_c; h++) {
							int y = c * h_per_c + h + record_time_c.where.y;
							SDL_RenderLine(screen, record_time_c.where.x, y, record_time_c.where.x + record_time_c.where.w * avg_per_channel[c], y);
						}
					}
				}
				else {
					std::vector<float> scope;  // one channel
					scope.resize(scope_in.size() / n_channels);

					for(int c=0; c<std::min(n_channels, 2); c++) {
						for(size_t i=0, s_index=0; i<scope_in.size(); i += n_channels, s_index++)
							scope[s_index] = scope_in[i + c];

						draw_scope(screen, scope_c.where, scope, c);
					}
				}

				if (sound_pars.record_wav_smf_since) {
					char buffer[13];
					uint64_t ms_running = (get_us() - sound_pars.record_wav_smf_since) / 1000;
					snprintf(buffer, sizeof buffer, "%02d:%02d:%02d.%03d", 
							int(ms_running / (3600 * 1000)),
							int(ms_running / (  60 * 1000) % 60),
							int(ms_running / (       1000) % 60),
							int(ms_running % 1000));
					draw_text(font, screen, record_time_c.where.x, record_time_c.where.y, buffer, { { record_time_c.where.w, record_time_c.where.h } });
				}
			}
			else if (mode == m_sample) {
				std::unique_lock<std::shared_mutex> sp_lck(sound_pars.sounds_lock);
				int                 vol_left  = 0;
				int                 vol_right = 0;
				auto               &sample    = samples[fs_action_sample_index];
				sound_sample *const s         = sample.s;
				auto                midi_note = sample.midi_note;
				int                 echo_t    = sample.echo_t;
				if (s) {
					vol_left  = s->get_volume(0) * 100;
					vol_right = s->get_volume(1) * 100;
				}
				std::string name = samples[fs_action_sample_index].name;
				if (s)
					name += " [" + std::to_string(sample.s->get_n_channels()) + "]";
				sp_lck.unlock();

				if (name.empty() == false)
					draw_text(font, screen, 0, win_height - font_height * 5, get_filename(name), { { win_width, font_height } });
				draw_clickables(font, font_small, screen, channel_clickables, { }, pattern_group);
				draw_clickables(font, font_small, screen, channel_buttons_clickables, { }, { });
				draw_text(font, screen, sample_vol_widget_left.x,  sample_vol_widget_left.y,  std::to_string(vol_left),
					{ { sample_vol_widget_left.text_w,  sample_vol_widget_left.text_h } });
				draw_text(font, screen, sample_vol_widget_right.x, sample_vol_widget_right.y, std::to_string(vol_right),
					{ { sample_vol_widget_right.text_w, sample_vol_widget_right.text_h } });
				if (midi_note.has_value()) {
					draw_text(font, screen, midi_note_widget_pars.x, midi_note_widget_pars.y,  std::to_string(midi_note.value() + 1),
						{ { midi_note_widget_pars.text_w,  midi_note_widget_pars.text_h } });
				}
				draw_text(font, screen, pitch_pars.x, pitch_pars.y, std::to_string(s ? s->get_pitch_bend() : 0),
					{ { pitch_pars.text_w, pitch_pars.text_h } });
				draw_text(font, screen, echo_t_pars.x, echo_t_pars.y, std::to_string(echo_t),
					{ { echo_t_pars.text_w, echo_t_pars.text_h } });

				std::unique_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
				pattern & pat       = pat_clickables[fs_action_sample_index];
				size_t    dim       = pat.dim;
				auto      lp_cutoff = pat.lp_cutoff;
				auto      hp_cutoff = pat.hp_cutoff;
				auto      swing     = pat.swing;
				auto      delay     = pat.delay;
				pat_lck.unlock();

				draw_text(font, screen, n_steps_pars.x, n_steps_pars.y, std::to_string(dim),
					{ { n_steps_pars.text_w, n_steps_pars.text_h } });
				if (lp_cutoff.has_value())
					draw_text(font, screen, lp_filter_widget.x, lp_filter_widget.y, std::to_string(int(lp_cutoff.value())), { { lp_filter_widget.text_w, lp_filter_widget.text_h } });
				if (hp_cutoff.has_value())
					draw_text(font, screen, hp_filter_widget.x, hp_filter_widget.y, std::to_string(int(hp_cutoff.value())), { { hp_filter_widget.text_w, hp_filter_widget.text_h } });
				draw_text(font, screen, swing_factor_widget.x, swing_factor_widget.y, std::to_string(swing), { { swing_factor_widget.text_w, swing_factor_widget.text_h } });
				draw_text(font, screen, delay_factor_widget.x, delay_factor_widget.y, std::to_string(delay), { { delay_factor_widget.text_w, delay_factor_widget.text_h } });
			}
			else if (mode == m_cell) {
				std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
				auto & pattern = pat_clickables[pattern_group];

				draw_clickables(font, font_small, screen, cell_menu_buttons, { }, { });
				draw_text(font, screen, pitch_widget.x, pitch_widget.y, pattern.pattern[selected_cell].text,
					{ { pitch_widget.text_w, pitch_widget.text_h } });
				draw_text(font, screen, cell_volume_left_widget.x, cell_volume_left_widget.y, std::to_string(pattern.volume_left[selected_cell]),
					{ { cell_volume_left_widget.text_w, cell_volume_left_widget.text_h } });
				draw_text(font, screen, cell_volume_right_widget.x, cell_volume_right_widget.y, std::to_string(pattern.volume_right[selected_cell]),
					{ { cell_volume_right_widget.text_w, cell_volume_right_widget.text_h } });
			}
			else if (mode == m_midi) {
				std::string name;
				{
					std::unique_lock<std::mutex> lck(sound_pars.midi_sample_lock);
					if (sound_pars.midi_sample.s)
						name = sound_pars.midi_sample.name;
					if (name.empty())
						name = PROG_NAME " " KABOEM_VERSION;
				}
				draw_text(font, screen, 0, 0, name, { { win_width, win_height } }, false, text_alignment::left, text_alignment::bottom);

				if (selected_percussion_midi_channel.has_value()) {
					draw_text(font, screen, midi_ch_widget.x, midi_ch_widget.y,  std::to_string(selected_percussion_midi_channel.value() + 1),
						{ { midi_ch_widget.text_w, midi_ch_widget.text_h } });
				}

				draw_text(font, screen, midi_volume_left_widget.x, midi_volume_left_widget.y, std::to_string(midi_volume_left),
					{ { midi_volume_left_widget.text_w, midi_volume_left_widget.text_h } });

				draw_text(font, screen, midi_volume_right_widget.x, midi_volume_right_widget.y, std::to_string(midi_volume_right),
					{ { midi_volume_right_widget.text_w, midi_volume_right_widget.text_h } });

				draw_clickables(font, font_small, screen, menu_button_clickables, { }, { });
				draw_clickables(font, font_small, screen, midi_menu_buttons,      { }, { });
			}
			else {
				fprintf(stderr, "Internal error: %d\n", mode);
				break;
			}

			SDL_RenderPresent(screen);
			redraw = false;
		}

		// process mouse clicks etc
		SDL_Event event { 0 };
		while(SDL_WaitEventTimeout(&event, 1)) {
			if (event.type == SDL_EVENT_QUIT) {
				do_exit = true;
				break;
			}

			float ignore = 0.;
			SDL_MouseButtonFlags mouse_button_flags = SDL_GetMouseState(&ignore, &ignore);

			if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && (mouse_button_flags & 1) /* left button */) || event.type == SDL_EVENT_KEY_UP) {
				if (mode == m_pattern) {
					auto menu_clicked = find_clickable(menu_button_clickables, event);
					if (menu_clicked.has_value()) {
						if (mode == m_pattern)
							mode = m_settings;
						else if (mode == m_sample)
							mode = m_settings;
						else
							mode = m_pattern;
					}
					else {
						auto p_menu_clicked = find_clickable(pattern_menu,       event);
						auto new_group      = find_clickable(channel_clickables, event);
						if (new_group.has_value()) {
							channel_clickables[pattern_group].selected = false;
							pattern_group = new_group.value();
							channel_clickables[pattern_group].selected = true;
						}
						else if (p_menu_clicked.has_value()) {
							size_t idx = p_menu_clicked.value();
							if (idx == p_pause_idx) {
								paused = !paused;
							}
							else if (idx == restart_idx) {
								start_t = get_us();
								paused  = false;
							}
							pattern_menu         [p_pause_idx].selected = paused;
							settings_menu_buttons[pause_idx]  .selected = paused;
						}
						else {
							std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
							pat_clickable_selected = find_clickable(pat_clickables[pattern_group].pattern, event);
							if (pat_clickable_selected.has_value())
								pat_clickable_pressed_since = get_ms();
						}
					}
				}
				else if (mode == m_settings) {
					menu_status.clear();
					auto menu_clicked   = find_clickable(menu_button_clickables, event);
					auto sample_clicked = find_clickable(channel_clickables,     event);
					auto menus_clicked  = find_clickable(settings_menu_buttons,  event);
					if (menu_clicked.has_value()) {
						if (mode == m_pattern)
							mode = m_settings;
						else
							mode = m_pattern;
					}
					else if (menus_clicked.has_value()) {
						size_t idx = menus_clicked.value();
						if (idx == clear_idx) {
							bool choice = are_you_sure(font, font_small, screen, win_width, win_height, font_height, "Clear everything");
							if (choice) {
								draw_please_wait(font, screen, win_width, win_height);
								clear_everything(pat_clickables, &pat_clickables_lock, sound_pars, &menu_status, work_path,
										samples, file_parameters, channel_clickables, &kaboem_file);
								menu_status = "cleared";
							}

							redraw = true;
						}
						else if (idx == pattern_load_idx) {
							if (kaboem_file.empty() || are_you_sure(font, font_small, screen, win_width, win_height, font_height, "Load")) {
								fs_data.finished = false;
								fs_action        = fs_load;
								SDL_ShowOpenFileDialog(fs_callback, &fs_data, win, sf_filters, 1, work_path.c_str(), false);
							}
						}
						else if (idx == save_idx) {
							fs_data.finished = false;
							fs_action = fs_save;
							SDL_ShowSaveFileDialog(fs_callback, &fs_data, win, sf_filters, 1, work_path.c_str());
						}
						else if (idx == midi_idx) {
							mode = m_midi;
						}
						else if (idx == quit_idx) {
							if (are_you_sure(font, font_small, screen, win_width, win_height, font_height, "Quit"))
								do_exit = true;
						}
						else if (set_up_down_value(idx, humanize_widget, 0, 1000, &humanize_amount, shift)) {
							humanize_amount_parameter = humanize_amount;
						}
						else if (set_up_down_value(idx, bpm_widget, 1, 999, &bpm, shift)) {
						}
						else if (set_up_down_value(idx, vol_widget, 0, 110, &vol, shift)) {  // this one goes to 11!
						}
						else if (set_up_down_value(idx, sound_saturation_widget, 0, 1000, &sound_saturation, shift)) {
							std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
							sound_pars.sound_saturation = 1. - sound_saturation / 1000.;
						}
						else if (idx == record_idx) {
							bool was_writing = false;
#if HAVE_SMF == 1
							{
								std::unique_lock<std::mutex> s_lck(sound_pars.smf_lock);
								if (sound_pars.smf) {
									if (close_mid_file(&sound_pars) == false)
										menu_status = "MIDI file writing failed";
									sound_pars.smf = nullptr;
									was_writing    = true;
								}
							}
#endif

							{
								std::unique_lock<std::mutex> r_lck(sound_pars.record_lock);
								if (sound_pars.record_handle) {
									sf_close(sound_pars.record_handle);
									sound_pars.record_handle = nullptr;
									was_writing              = true;
								}
							}

							if (was_writing == true) {
								menu_status                                = "recording stopped";
								settings_menu_buttons[record_idx].selected = false;
								sound_pars.record_wav_smf_since            = 0;
							}
							else {
								fs_data.finished = false;
								fs_action        = fs_record;
								SDL_ShowSaveFileDialog(fs_callback, &fs_data, win, sf_filters_record, 1, work_path.c_str());
							}
						}
						else if (idx == pause_idx) {
							paused = !paused;
							settings_menu_buttons[pause_idx].selected = paused;
						}
						else if (idx == polyrythmic_idx) {
							polyrythmic = !polyrythmic;
							settings_menu_buttons[polyrythmic_idx].selected = polyrythmic;
						}
						else if (idx == agc_idx) {
							agc = !agc;
							settings_menu_buttons[agc_idx].selected = agc;
						}
						else if (idx == scope_stereo_idx) {
							scope_stereo = !scope_stereo;
							settings_menu_buttons[scope_stereo_idx].selected = scope_stereo;
						}
						set_bpm_sleep(&sleep_us, bpm);
						std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
						sound_pars.global_volume = vol / 100.;
						sound_pars.agc_enabled   = agc;
					}
					else if (sample_clicked.has_value()) {
						mode = m_sample;
						channel_clickables[fs_action_sample_index].selected = false;
						fs_action_sample_index = sample_clicked.value();
						channel_clickables[fs_action_sample_index].selected = true;
						fs_data.finished = false;
						set_samples_buttons(sound_pars, samples, channel_buttons_clickables, fs_action_sample_index, mute_idx, serial_notes_idx, pat_clickables[fs_action_sample_index].serial_notes);
					}
				}
				else if (mode == m_sample) {
					auto menu_clicked   = find_clickable(menu_button_clickables,     event);
					auto sample_clicked = find_clickable(channel_clickables,         event);
					auto menus_clicked  = find_clickable(channel_buttons_clickables, event);
					if (menu_clicked.has_value()) {
						mode = m_settings;
						channel_clickables[fs_action_sample_index].selected = false;
					}
					else if (sample_clicked.has_value()) {
						channel_clickables[fs_action_sample_index].selected = false;
						fs_action_sample_index = sample_clicked.value();
						channel_clickables[fs_action_sample_index].selected = true;
						fs_data.finished = false;

						set_samples_buttons(sound_pars, samples, channel_buttons_clickables, fs_action_sample_index, mute_idx, serial_notes_idx, pat_clickables[fs_action_sample_index].serial_notes);
					}
					else if (menus_clicked.has_value()) {
						size_t idx = menus_clicked.value();
						if (idx == sample_load_idx) {
							fs_data.finished = false;
							fs_action        = fs_load_sample;
							SDL_ShowOpenFileDialog(fs_callback, &fs_data, win, sf_filters_sample, 1, work_path.c_str(), false);
						}
						else if (configure_filter(&pat_clickables[fs_action_sample_index], &pat_clickables_lock, lp_filter_widget,
									idx, false, shift)) {
							// taken
						}
						else if (configure_filter(&pat_clickables[fs_action_sample_index], &pat_clickables_lock, hp_filter_widget,
									idx, true, shift)) {
							// taken
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
							if (s) {
								bool new_state = !s->get_mute();
								s->set_mute(new_state);
								channel_buttons_clickables[mute_idx].selected = new_state;
							}
							else {
								channel_buttons_clickables[mute_idx].selected = false;
							}
						}
						else if (idx == serial_notes_idx) {
							{
								std::lock_guard<std::shared_mutex> lck(pat_clickables_lock);
								pat_clickables[fs_action_sample_index].serial_notes = !pat_clickables[fs_action_sample_index].serial_notes;
							}

							set_samples_buttons(sound_pars, samples, channel_buttons_clickables, fs_action_sample_index, mute_idx, serial_notes_idx, pat_clickables[fs_action_sample_index].serial_notes);
						}
						else {
							std::lock_guard<std::shared_mutex> lck(sound_pars.sounds_lock);
							sample       *const sample_   = &samples[fs_action_sample_index];
							sound_sample *const s         = sample_->s;
							auto              & midi_note = sample_->midi_note;
							int                 pitch     = s ? s->get_pitch_bend() * 1000 : 0;

							if (set_up_down_value(idx, midi_note_widget_pars, 0, 127, &midi_note, shift)) {
								// taken
							}
							else if (set_up_down_value(idx, pitch_pars, 0, 10000, &pitch, shift)) {
								if (s)
									s->set_pitch_bend(pitch / 1000.);
							}
							else if (set_up_down_value(idx, echo_t_pars, 0, 10000, &sample_->echo_t, shift)) {
								update_queued_sounds(sound_pars.sounds, sample_);
							}
							else if (idx == n_steps_pars.up) {
								pat_clickables[fs_action_sample_index].dim = std::min(max_pattern_dim, pat_clickables[fs_action_sample_index].dim + 1);
								regenerate_pattern_grid(win_width, win_height, &pat_clickables[fs_action_sample_index]);
							}
							else if (idx == n_steps_pars.down) {
								pat_clickables[fs_action_sample_index].dim = std::max(size_t(2), pat_clickables[fs_action_sample_index].dim - 1);
								regenerate_pattern_grid(win_width, win_height, &pat_clickables[fs_action_sample_index]);
							}
							else if (s == nullptr) {
								// skip volume when no sample
							}
							else if (configure_volume(&sound_pars, sample_vol_widget_left, idx, s, 0, shift)) {
								update_queued_sounds(sound_pars.sounds, sample_);
							}
							else if (configure_volume(&sound_pars, sample_vol_widget_right, idx, s, 1, shift)) {
								update_queued_sounds(sound_pars.sounds, sample_);
							}
							else {
								std::unique_lock<std::shared_mutex> lck(pat_clickables_lock);

								int max_swing = sleep_us * 4;
								if (set_up_down_value(idx, swing_factor_widget, 0, max_swing, &pat_clickables[fs_action_sample_index].swing, shift)) {
									// ok
								}
								else if (set_up_down_value(idx, delay_factor_widget, -1000, 1000, &pat_clickables[fs_action_sample_index].delay, shift)) {
									// ok
								}
							}
						}
					}
				}
				else if (mode == m_cell) {
					auto menu_clicked = find_clickable(menu_button_clickables, event);
					auto idx          = find_clickable(cell_menu_buttons,      event);
					if (menu_clicked.has_value())
						mode = m_pattern;
					else if (idx.has_value()) {
						std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
						auto & pattern = pat_clickables[pattern_group];

						if (set_up_down_value(idx.value(), pitch_widget, 0, 127, &pattern.note_delta[selected_cell], shift)) {
							std::unique_lock<std::shared_mutex> lck(sound_pars.sounds_lock);
							sound_sample *const s = samples[pattern_group].s;
							if (s)
								pattern.pattern[selected_cell].text = midi_note_to_name(s->get_base_midi_note() + pattern.note_delta[selected_cell]);
						}
						else if (set_up_down_value(idx.value(), cell_volume_left_widget,  &pattern.volume_left [selected_cell], shift)) {
							// ok
						}
						else if (set_up_down_value(idx.value(), cell_volume_right_widget, &pattern.volume_right[selected_cell], shift)) {
							// ok
						}
						else {
						}
					}
				}
				else if (mode == m_midi) {
					auto menu_clicked = find_clickable(menu_button_clickables, event);
					auto midi_clicked = find_clickable(midi_menu_buttons,      event);
					if (menu_clicked.has_value())
						mode = m_settings;
					else if (midi_clicked.has_value()) {
						size_t idx = midi_clicked.value();
						if (set_up_down_value(idx, midi_ch_widget, 0, 15, &selected_percussion_midi_channel, shift)) {
							if (selected_percussion_midi_channel.has_value())
								midi_thread_selected_percussion_midi_channel = selected_percussion_midi_channel.value();
							else
								midi_thread_selected_percussion_midi_channel = -1;
						}
						else if (idx == load_midi_sample_idx) {
							fs_data.finished = false;
							fs_action        = fs_load_midi_sample;
							SDL_ShowOpenFileDialog(fs_callback, &fs_data, win, sf_filters_sf2, 1, work_path.c_str(), false);
						}
						else if (set_up_down_value(idx, midi_volume_left_widget,  0, 127, &midi_volume_left,  shift)) {
							midi_update_global_volume(&sound_pars, midi_volume_left, midi_volume_right);
						}
						else if (set_up_down_value(idx, midi_volume_right_widget, 0, 127, &midi_volume_right, shift)) {
							midi_update_global_volume(&sound_pars, midi_volume_left, midi_volume_right);
						}
					}
				}

				redraw = true;
			}
			else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && (mouse_button_flags & 4) /* right button */) {
				std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);

				pat_clickable_selected = find_clickable(pat_clickables[pattern_group].pattern, event);
				if (pat_clickable_selected.has_value()) {
					mode          = m_cell;
					selected_cell = pat_clickable_selected.value();
				}
			}
			else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
				uint64_t now = get_ms();

				std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
				if (pat_clickable_selected.has_value()) {
					if (now - pat_clickable_pressed_since > long_press_dt) {  // long press?
						// cell menu
						mode          = m_cell;
						selected_cell = pat_clickable_selected.value();
					}
					else {
						pat_clickables[pattern_group].pattern[pat_clickable_selected.value()].selected =
							!pat_clickables[pattern_group].pattern[pat_clickable_selected.value()].selected;
					}

					pat_clickable_selected.reset();

					redraw = true;
				}
			}
			else if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.scancode == SDL_SCANCODE_SPACE) {
					std::lock_guard<std::shared_mutex> pat_lck(pat_clickables_lock);
					pattern & p = pat_clickables[pattern_group];
					if (p.cursor.has_value()) {
						size_t cursor_idx = p.cursor.value().second * p.wdim + p.cursor.value().first;
						p.pattern[cursor_idx].selected = !p.pattern[cursor_idx].selected;
					}
					else {
						p.pattern[pat_index].selected = !p.pattern[pat_index].selected;
					}

					redraw        = true;
					force_trigger = true;
				}
				else if (event.key.scancode == SDL_SCANCODE_LEFT) {
					pattern & p = pat_clickables[pattern_group];
					if (p.cursor.has_value() == false)
						p.cursor = { 0, 0 };
					else if (p.cursor.value().first == 0) {
						if (p.cursor.value().second) {
							p.cursor.value().second--;
							p.cursor.value().first = p.wdim - 1;
						}
						else {
							p.cursor.value().second = p.hdim - 1;

							auto h_index = p.cursor.value().second * p.wdim;
							if (h_index + p.cursor.value().first >= p.dim)
								p.cursor.value().first = p.dim - h_index - 1;
							else
								p.cursor.value().first = p.wdim - 1;
						}
					}
					else {
						p.cursor.value().first--;
					}
				}
				else if (event.key.scancode == SDL_SCANCODE_RIGHT) {
					pattern & p = pat_clickables[pattern_group];
					if (p.cursor.has_value() == false)
						p.cursor = { 0, 0 };
					else if (p.cursor.value().first == int(p.wdim - 1)) {
						if (p.cursor.value().second < int(p.hdim - 1)) {
							p.cursor.value().second++;
							p.cursor.value().first = 0;
						}
						else {
							p.cursor.value().second = 0;
							p.cursor.value().first = 0;
						}
					}
					else {
						p.cursor.value().first++;
					}
				}
				else if (event.key.scancode == SDL_SCANCODE_UP) {
					pattern & p = pat_clickables[pattern_group];
					if (p.cursor.has_value() == false)
						p.cursor = { 0, 0 };
					else if (p.cursor.value().second == 0) {
						if (p.cursor.value().first > 0)
							p.cursor.value().first--;
						else
							p.cursor.value().first = p.wdim - 1;
						p.cursor.value().second = p.hdim - 1;

						if (get_cursor_index(p) >= p.dim)
							p.cursor.value().second = 0;
					}
					else {
						p.cursor.value().second--;
					}
				}
				else if (event.key.scancode == SDL_SCANCODE_DOWN) {
					pattern & p = pat_clickables[pattern_group];
					if (p.cursor.has_value() == false)
						p.cursor = { 0, 0 };
					else if (p.cursor.value().second == int(p.hdim - 1)) {
						p.cursor.value().second = 0;

						if (p.cursor.value().first < p.wdim - 1)
							p.cursor.value().first++;
						else
							p.cursor.value().first = 0;
					}
					else {
						p.cursor.value().second++;
					}
				}
				else if (event.key.scancode == SDL_SCANCODE_LSHIFT || event.key.scancode == SDL_SCANCODE_RSHIFT) {
					shift = true;
				}
				else if (event.key.scancode == SDL_SCANCODE_LCTRL || event.key.scancode == SDL_SCANCODE_RCTRL) {
					ctrl = true;
				}
			}
			else if (event.type == SDL_EVENT_KEY_UP) {
				if (event.key.scancode == SDL_SCANCODE_LSHIFT || event.key.scancode == SDL_SCANCODE_RSHIFT)
					shift = false;
				else if (event.key.scancode == SDL_SCANCODE_LCTRL || event.key.scancode == SDL_SCANCODE_RCTRL)
					ctrl = false;
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

	draw_please_wait(font, screen, win_width, win_height);

	midi_thread  .join();
	player_thread.join();
	mixer_thread .join();
	stop_sdl3_audio(&sound_pars);

	{  // stop any recording
		std::unique_lock<std::mutex> r_lck(sound_pars.record_lock);
		if (sound_pars.record_handle)
			sf_close(sound_pars.record_handle);
	}
	{
#if HAVE_SMF == 1
		std::unique_lock<std::mutex> lck(sound_pars.smf_lock);
		if (sound_pars.smf)
			close_mid_file(&sound_pars);
#endif
	}

	{
		std::shared_lock<std::shared_mutex> pat_lck(pat_clickables_lock);
		write_file(work_path + "/default." PROG_EXT, pat_clickables, samples, file_parameters, &sound_pars.midi_sample);
	}

	SDL_Quit();
	deinit_fonts();

	close_midi_in_port(midi_in);
	deinit_midi();

	return 0;
}
