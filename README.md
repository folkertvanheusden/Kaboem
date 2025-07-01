This software was written by Folkert van Heusden. License: MIT.

It specifically targets a raspberry pi with a touchscreen altough other Linux systems will work as well.

To be able to compile and run this program, you need to install:

```
sudo apt install fonts-freefont-ttf libpipewire-0.3-dev libsdl3-dev libsdl3-ttf-dev libsndfile1-dev nlohmann-json3-dev libfftw3-dev build-essential cmake
```

Because of the SDL3 requirement, this program requires at least Debian Trixie (13), Ubuntu Plucky (25.04) or equivalent (or more recent of course).

To compile:
```
mkdir build
cd build
cmake ..
make
```
The executable will then named 'kaboem'.

Please note that this software is not even an alpha version. Work in progress!

The main screen shows 16 steps. In the settings-menu this can be changed to 3 upto 32 steps.
On the right you see the 8 channels that you can select. At the top-right there's the button to switch between settings and edit mode.

![main screen with a pattern](images/kaboem-main-w-pattern.png)

Pressing "record" will record to a .wav-file.
Load/save are for writing the current song to a .kaboem-file for later re-edit.
In the settings-menu, click on a channel will open a channel-edit menu.
Pressing menu again will bring you back to the pattern-editor.

![settings screen](images/kaboem-settings.png)

In the channel-edit menu, load/unload are for loading a sample in the channel.
Pressing the menu-button again brings you back the main-settings screen.

![settings of a channel](images/kaboem-channel-settings.png)
