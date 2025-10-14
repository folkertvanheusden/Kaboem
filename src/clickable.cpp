#include "clickable.h"
#include "font.h"


clickable::clickable(const SDL_Rect & where, const std::string & text, const bool without_bg, const char key):
	without_bg(without_bg),
	key(key),
	where(where),
	text(text)
{
}

clickable::clickable()
{
}

clickable::~clickable()
{
}

clickable clickable::operator=(const clickable & from)
{
	where      = from.where;
	text       = from.text;
	without_bg = from.without_bg;
	key        = from.key;
	return *this;
}

void clickable::draw(TTF_Font *const font_big, TTF_Font *const font_small, SDL_Renderer *const screen, const std::vector<int> & color) const
{
	float     x1 = where.x;
	float     y1 = where.y;
	SDL_FRect r    { x1, y1, float(where.w), float(where.h) };
	if (without_bg == false) {
		SDL_SetRenderDrawColor(screen, color[0], color[1], color[2], 255);
		SDL_RenderFillRect(screen, &r);
	}
	SDL_SetRenderDrawColor(screen, 40, 40, 40, 191);
	SDL_RenderRect(screen, &r);

	if (text.empty() == false)
		draw_text(font_big, screen, x1, y1, text, { { where.w, where.h } });

	if (key) {
		std::string key_str = key < 27 ? std::string("^") + char(key + 'a' - 1) : std::string(1, key);
		SDL_SetRenderDrawColor(screen, (color[0] + 40) / 2, (color[1] + 40) / 2, (color[2] + 40) / 2, 255);
		draw_text(font_small, screen, x1, y1, key_str, { { where.w, where.h } }, false, text_alignment::left, text_alignment::bottom);
	}
}

bool clickable::is_triggered(const SDL_Event & in) const
{
	if (in.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		const int x = in.button.x;
		const int y = in.button.y;
                return  x >= where.x &&
			y >= where.y &&
			x < where.x + where.w &&
			y < where.y + where.h;
        }

        return false;
}
