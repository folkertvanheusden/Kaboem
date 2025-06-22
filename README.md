This software was written by Folkert van Heusden. License: MIT.

It specifically targets a raspberry pi with a touchscreen altough other Linux systems will work as well.

small-reverb-bass-drum-sound-a-key-10-G8d.wav and studio-hihat-sound-a-key-05-yvg.wav came from https://soundcamp.org/tag/tuned-kick-drums where they were advertised as royalty free (2025/06/21).


To be able to compile and run this program, you need to install:

    fonts-freefont-ttf
    libpipewire-0.3-dev
    libsdl3-dev
    libsdl3-ttf-dev
    libsndfile1-dev
    nlohmann-json3-dev

To compile:
* mkdir build
* cd build
* cmake ..
* make
