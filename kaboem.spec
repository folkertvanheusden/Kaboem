Name:       kaboem
Version:    0.0010
Release:    0
Summary:    An audio sequencer
License:    MIT
Source0:    %{name}-%{version}.tgz
URL:        https://github.com/folkertvanheusden/kaboem
BuildRequires: g++ cmake SDL3-devel SDL3_ttf-devel libsndfile-devel rtmidi-devel json-devel fftw-devel
Requires:   SDL3 SDL3_ttf libsndfile rtmidi fftw

%description
Kaboem is a simple (MIDI enabled) audio sequencer.

%prep
%setup -q -n %{name}-%{version}

%build
cmake -S . -B redhat-linux-build -DCMAKE_INSTALL_PREFIX=%{_prefix}
cmake --build redhat-linux-build

%install
%cmake_install

%files
/usr/bin/kaboem
/usr/share/kaboem
/usr/share/applications/kaboem.desktop
/usr/share/icons/hicolor/256x256/kaboem.png

%changelog
* Thu Jul 11 2025 Folkert van Heusden <folkert@vanheusden.com>
-
