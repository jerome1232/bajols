%global debug_package %{nil}
%global __os_install_post %{nil}

Name: bajols-groundstation
Version: %{package_version}
Release: 1
Summary: BAJOLS vehicle ground station
License: GPL-3.0-only
URL: https://github.com/jerome1232/bajols
AutoReqProv: no
Requires: ca-certificates, glibc, libgcc, libstdc++, krb5-libs, libicu, openssl-libs, tzdata, zlib
Requires: fontconfig, libX11, libXext, libXi, libXrandr, libXcursor, libICE, libSM
Recommends: libSDL2-2.0.so.0()(64bit)

%description
Desktop application for vehicle telemetry, control and simulation.

%install
mkdir -p "%{buildroot}/usr"
cp -a "%{package_root}/usr/." "%{buildroot}/usr/"

%files
%license /usr/share/doc/%{name}/copyright
/usr/bin/%{name}
/usr/lib/%{name}
/usr/share/applications/%{name}.desktop
