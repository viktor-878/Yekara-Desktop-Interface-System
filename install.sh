#!/bin/sh

PREFIX="${1:-/usr}"

echo "Installing Yekara into $PREFIX"

# STUFF
cp YK_GLOBAL_ENVVARS /etc/profile.d/YK_VARS.sh

# binaries
install -d "$PREFIX/bin"

install -m755 scripts/YKBackdrop "$PREFIX/bin"
install -d /system
install -d /system/resources # for splash.xpm
cp ./splash.xpm /system/resources/splash.xpm

# library
cd libYekara
make -lXm -lXt -lX11
make install PREFIX="$PREFIX"
cd ..

cd ykclock
./configure
make
make install PREFIX=/usr
cd ..

# TextBook
cd TextBook
make
install -d "/applications"
make install
cd ..

cd filecabinet
make
install -d "/applications"
make install
cd ..

rm -rf /applications/"Command Center.deskapp"

cd "Command Center"
make
install -d "/applications"
make install
cd ..

# FVWM configuration | MOVED TO YKWM

# cursors
rm -rf "$PREFIX/share/icons/yekara-cursors"
install -d "$PREFIX/share/icons/yekara-cursors"
cd yekara-cursor
./finish.sh
cd ..
mv yekara-cursor/build/* "$PREFIX/share/icons/"

echo "Done."
