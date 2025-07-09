#pragma once

#include <thread>
#include <SDL3/SDL.h>


class sdl3_data_audio
{
public:
	SDL_AudioSpec    spec   {         };
	SDL_AudioStream *stream { nullptr };
	int              frames { 1024    };
};

class sound_parameters;

bool configure_sdl3_audio(sound_parameters *const pw);
void stop_sdl3_audio     (sound_parameters *const pw);
