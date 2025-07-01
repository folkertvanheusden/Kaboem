#include <string>
#include <vector>
#include <SDL3_ttf/SDL_ttf.h>


void       init_fonts();
void       deinit_fonts();
TTF_Font * load_font(const std::vector<std::string> & font_names, unsigned int font_height, bool fast_rendering);
