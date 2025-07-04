Name:       kaboem
Version:    0.0006
Release:    0
Summary:    An audio sequencer
License:    MIT
Source0:    %{name}-%{version}.tgz
URL:        https://github.com/folkertvanheusden/kaboem
BuildRequires: g++ cmake SDL3-devel SDL3_ttf-devel libsndfile-devel alsa-lib-devel json-devel fftw-devel pipewire-devel
Requires:   SDL3 SDL3_ttf libsndfile alsa-lib fftw pipewire

%description
Kaboem is a simple pipewire and midi enabled audio sequencer.

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
* Fri Jul 4 2025 Folkert van Heusden <folkert@vanheusden.com>
-
