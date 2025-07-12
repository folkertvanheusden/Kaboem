#include <string>
#include <vector>
#include <SDL3_ttf/SDL_ttf.h>


void       init_fonts();
void       deinit_fonts();
TTF_Font * load_font(const std::vector<std::string> & font_names, unsigned int font_height, bool fast_rendering);
TTF_Font * load_font_by_filename(const std::string & filename, const unsigned int font_heigiht, const bool fast_rendering);
