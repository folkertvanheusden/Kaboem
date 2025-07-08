Name:       kaboem
Version:    0.0008
Release:    0
Summary:    An audio sequencer
License:    MIT
Source0:    %{name}-%{version}.tgz
URL:        https://github.com/folkertvanheusden/kaboem
BuildRequires: g++ cmake SDL3-devel SDL3_ttf-devel libsndfile-devel alsa-lib-devel json-devel fftw-devel
Requires:   SDL3 SDL3_ttf libsndfile alsa-lib fftw

%description
Kaboem is a simple (MIDI enabled) audio sequencer.

%prep
%setup -q -n %{name}-%{version}

%build
%cmake .
%cmake_build

%install
%cmake_install

%files
/usr/bin/kaboem
/usr/share/kaboem

%changelog
* Sun Jul 6 2025 Folkert van Heusden <folkert@vanheusden.com>
-
