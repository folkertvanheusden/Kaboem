#include <string>
#include <vector>
#include <SDL3_ttf/SDL_ttf.h>


void       init_fonts();
void       deinit_fonts();
TTF_Font * load_font(const std::vector<std::string> & font_names, unsigned int font_height, bool fast_rendering);
TTF_Font * load_font_by_filenames(const std::vector<std::string> & filenames, const unsigned int font_height, const bool fast_rendering);

enum text_alignment { left = 0, top = 0, right = 1, bottom = 1, center = 2 };

void draw_text(TTF_Font *const font, SDL_Renderer *const screen, const int x, const int y, const std::string & text,
	       const std::optional<std::pair<int, int> > & in, const bool important = false,
	       const text_alignment h_alignment = text_alignment::center, const text_alignment v_alignment = text_alignment::center);
