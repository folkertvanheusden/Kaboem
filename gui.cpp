#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>


bool do_exit = false;

std::vector<SDL_FRect> draw_pattern_edit(SDL_Renderer *const screen, const int steps)
{
	int w = 0;
	int h = 0;
	SDL_GetCurrentRenderOutputSize(screen, &w, &h);
	printf("%dx%d\n", w, h);

	int pattern_w = w * 85 / 100;
	int pattern_h = h;

	int sq_steps  = sqrt(steps);
	int steps_w   = steps / sq_steps;
	int steps_h   = steps / steps_w;

	std::vector<SDL_FRect> boxes;

	int step_width  = pattern_w / steps_w;
	int step_height = pattern_h / steps_h;

	for(int i=0; i<steps; i++) {
		int x = (i % steps_w) * step_width;
		int y = (i / steps_w) * step_height;
		//printf("%d,%d %dx%d\n", x, y, step_width, step_height);
		SDL_FRect rect { x, y, step_width, step_height };
		SDL_SetRenderDrawColor(screen, 40, 255, 40, SDL_ALPHA_OPAQUE);
		SDL_RenderFillRect(screen, &rect);
		SDL_SetRenderDrawColor(screen, 40, 40, 40, SDL_ALPHA_OPAQUE);
		SDL_RenderRect(screen, &rect);
		boxes.push_back(rect);
	}

	return boxes;
}

/* We will use this renderer to draw into this window every frame. */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("Kaboem", "0.1", "com.vanheusden.kaboem");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("Kaboem", 800, 600, 0, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    SDL_RenderClear(renderer);

    std::vector<SDL_FRect> boxes = draw_pattern_edit(renderer, 16);
    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    /* SDL will clean up the window/renderer for us. */
}
