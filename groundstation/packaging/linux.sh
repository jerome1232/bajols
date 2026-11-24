#!/usr/bin/env bash
set -euo pipefail

publish=$(realpath "$1")
mkdir -p "$2"
output=$(realpath "$2")
version=$3
packaging=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
root="$work/root"
package=bajols-groundstation

test -x "$publish/Bajols.GroundStation"
mkdir -p "$root/usr/lib/$package" "$root/usr/bin"
cp -a "$publish/." "$root/usr/lib/$package/"
ln -s "../lib/$package/Bajols.GroundStation" "$root/usr/bin/$package"
install -Dm644 "$packaging/$package.desktop" "$root/usr/share/applications/$package.desktop"
install -Dm644 "$packaging/../../LICENSE" "$root/usr/share/doc/$package/copyright"

mkdir "$root/DEBIAN"
cat > "$root/DEBIAN/control" <<CONTROL
Package: $package
Version: $version-1
Architecture: amd64
Maintainer: BAJOLS contributors <jerome1232@users.noreply.github.com>
Section: utils
Priority: optional
Homepage: https://github.com/jerome1232/bajols
Depends: ca-certificates, libc6, libgcc-s1, libstdc++6, libgssapi-krb5-2, libicu78 | libicu76 | libicu74 | libicu72 | libicu70, libssl3t64 | libssl3, tzdata, zlib1g, libfontconfig1, libx11-6, libxext6, libxi6, libxrandr2, libxcursor1, libice6, libsm6
Recommends: libsdl2-2.0-0 | libsdl2-compat
Description: BAJOLS vehicle ground station
 Desktop application for vehicle telemetry, control and simulation.
CONTROL
dpkg-deb --root-owner-group --build "$root" "$output/${package}_${version}-1_amd64.deb"

rpmbuild -bb "$packaging/$package.spec" \
    --target x86_64 \
    --define "_topdir $work/rpm" \
    --define "_tmppath $work" \
    --define "_rpmdir $work/rpm/RPMS" \
    --define "package_version $version" \
    --define "package_root $root"
cp "$work/rpm/RPMS/x86_64/"*.rpm "$output/"
