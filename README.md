This software was written by Folkert van Heusden. License: MIT.

It specifically targets a raspberry pi with a touchscreen altough other Linux systems will work as well.

To be able to compile and run this program, you need to install:

```
sudo apt install fonts-freefont-ttf libpipewire-0.3-dev libsdl3-dev libsdl3-ttf-dev libsndfile1-dev nlohmann-json3-dev libfftw3-dev build-essential cmake
```

Note that this requires at least Debian Trixie (13), Ubuntu Plucky (25.04) or equivalent (because of SDL3).

To compile:
```
mkdir build
cd build
cmake ..
make
```
The executable will then named 'kaboem'.

Please note that this software is not even an alpha version. Work in progress!

Preview:

![main screen with a pattern](images/kaboem-main-w-pattern.png)

![settings screen](images/kaboem-settings.png)

![settings of a channel](images/kaboem-channel-settings.png)
