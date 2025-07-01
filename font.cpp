#include <cstdio>
#include <string>
#include <vector>
#include <fontconfig/fontconfig.h>
#include <SDL3_ttf/SDL_ttf.h>


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

static TTF_Font * load_font(const std::string & font_name, unsigned int font_height, bool fast_rendering)
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
		printf("Font error: %s\n", SDL_GetError());
		return nullptr;
	}

        if (!fast_rendering)
                TTF_SetFontHinting(font, TTF_HINTING_NORMAL);

        return font;
}

TTF_Font * load_font(const std::vector<std::string> & font_names, unsigned int font_height, bool fast_rendering)
{
	for(auto & font_name : font_names) {
		TTF_Font *font = load_font(font_name, font_height, fast_rendering);
		if (font)
			return font;
	}

	return nullptr;
}
