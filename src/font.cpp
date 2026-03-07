#include <cassert>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>
#include <fontconfig/fontconfig.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "font.h"


void init_fonts()
{
	FcInit  ();
	TTF_Init();
}

void deinit_fonts()
{
	FcFini  ();
	TTF_Quit();
}

static TTF_Font * load_font(const std::string & font_name, const unsigned int font_height, const bool fast_rendering)
{
	FcPattern *pattern = FcNameParse(reinterpret_cast<const FcChar8 *>(font_name.c_str()));
	FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
	FcDefaultSubstitute(pattern);

	FcResult result { };
	FcPattern* match = FcFontMatch(nullptr, pattern, &result);

	if (!match)
		return nullptr;

	FcChar8    *file { nullptr };
	std::string font_path;
	if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
		font_path = reinterpret_cast<const char *>(file);
	FcPatternDestroy(match);

	FcPatternDestroy(pattern);

	if (font_path.empty())
		return nullptr;

        TTF_Font *font = TTF_OpenFont(font_path.c_str(), font_height);
	if (!font) {
		printf("Font error for \"%s\": %s\n", font_path.c_str(), SDL_GetError());
		return nullptr;
	}

        if (!fast_rendering)
                TTF_SetFontHinting(font, TTF_HINTING_NORMAL);

        return font;
}

TTF_Font * load_font(const std::vector<std::string> & font_names, const unsigned int font_height, const bool fast_rendering)
{
	for(auto & font_name : font_names) {
		TTF_Font *font = load_font(font_name, font_height, fast_rendering);
		if (font) {
			printf("Using font %s\n", font_name.c_str());
			return font;
		}
	}

	return nullptr;
}

TTF_Font * load_font_by_filenames(const std::vector<std::string> & filenames, const unsigned int font_height, const bool fast_rendering)
{
	for(auto & font_file : filenames) {
		TTF_Font *font = TTF_OpenFont(font_file.c_str(), font_height);
		if (font) {
			if (!fast_rendering)
				TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
			printf("Using font %s\n", font_file.c_str());
			return font;
		}
	}

	return nullptr;
}

void draw_text(TTF_Font *const font, SDL_Renderer *const screen, const int x, const int y, const std::string & text, const std::optional<std::pair<int, int> > & in,
	       const bool important, const text_alignment h_alignment, const text_alignment v_alignment)
{
	SDL_Surface *surface = nullptr;
	if (important)
		surface = TTF_RenderText_Solid(font, text.c_str(), 0, { 255, 192, 192, 255 });
	else
		surface = TTF_RenderText_Solid(font, text.c_str(), 0, { 192, 255, 192, 255 });
	if (!surface)  // empty strngs;
		return;

	SDL_Texture *texture = SDL_CreateTextureFromSurface(screen, surface);
	assert(texture);

	SDL_FRect dest { float(x), float(y), float(surface->w), float(surface->h) };
	if (in.has_value()) {
		if (h_alignment == text_alignment::center)
			dest.x = x + in.value().first / 2 - surface->w / 2;
		else if (h_alignment == text_alignment::right)
			dest.x = x + in.value().first - surface->w;

		if (v_alignment == text_alignment::center)
			dest.y = y + in.value().second / 2 - surface->h / 2;
		else if (v_alignment == text_alignment::bottom)
			dest.y = y + in.value().second - surface->h;
	}
	SDL_RenderTexture(screen, texture, nullptr, &dest);

	SDL_DestroyTexture(texture);
	SDL_DestroySurface(surface);
}

