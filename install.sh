#!/bin/sh

PREFIX="${1:-/usr}"

echo "Installing Yekara into $PREFIX"

# STUFF
cp YK_GLOBAL_ENVVARS /etc/profile.d/YK_VARS

# binaries
install -d "$PREFIX/bin"

install -m755 scripts/yekara "$PREFIX/bin/"
install -m755 scripts/YKBackdrop "$PREFIX/bin"
install -m755 scripts/readXColors "$PREFIX/bin/"

# library
cd libYekara
make -lXm -lXt -lX11
make install PREFIX="$PREFIX"
cd ..

gcc CSRC/sessionDiag.c -o "$PREFIX/bin/ykSessionDialog" -lXm -lXt -lX11 -lyk -larchive -lm
gcc CSRC/yk-applications-menu.c -o "$PREFIX/bin/yk-applications-menu"

cd ykclock
./configure
make
make install PREFIX=/usr


# TextBook
cd TextBook
make -lXm -lXt -lX11 -lyk
install -d "/applications"
rm -rf /applications/TextBook.deskapp
cp -r ./TextBook.deskapp "/applications/"
cd ..

# FVWM configuration
install -d "$PREFIX/share/yekara"
cp -r FVWMDEFS/* "$PREFIX/share/yekara"

# cursors
install -d "$PREFIX/share/icons/yekara-cursor"
cd yekara-cursor
./finish.sh
cd ..
cp -r yekara-cursor/build/* "$PREFIX/share/icons/yekara-cursor/"

echo "Done."
