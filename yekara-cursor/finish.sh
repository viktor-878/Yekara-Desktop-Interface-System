#!/bin/sh

set -e

THEME="yekara-cursors"
BUILD="build/$THEME"
OUT="$BUILD/cursors"
SRC="images"

mkdir -p "$OUT"
rm -f "$OUT"/*

echo "[*] Compiling cursors..."

gen () {
  NAME="$1"
  IMG="$2"
  HX="$3"
  HY="$4"

  CONF=$(mktemp)

  SIZE=$(identify -format "%w" "$SRC/$IMG")

  # dynamic edges
  [ "$HX" = "RIGHT" ] && HX=$((SIZE - 1))
  [ "$HY" = "BOTTOM" ] && HY=$((SIZE - 1))

  echo "$SIZE $HX $HY $SRC/$IMG" > "$CONF"
  xcursorgen "$CONF" "$OUT/$NAME"

  rm "$CONF"
}

# =================================================================
# YEKARA DESKTOP INTERFACE SYSTEM - CURSOR DEFINITIONS
# =================================================================

# ---------------- Core & Navigation ----------------
gen arrow               arrow.png 0 0
gen left_ptr            arrow.png 0 0
gen default             arrow.png 0 0
gen center_arrow        center_arrow.png 9 0

# ---------------- Interactive (Hand) ----------------
# Baseado no estilo BeOS para interação e DnD
gen hand                hand.png 3 1
gen hand1               grab.png 3 1
gen hand2               hand.png 3 1
gen pointer             hand.png 3 1
gen grab                grab.png 9 9
gen grabbing            grabbing.png 9 9

# ---------------- Selection & Precision ----------------
gen text                text.png 9 9
gen xterm               text.png 9 9
gen cross               cross.png 9 9
gen crosshair           cross.png 9 9
gen zoom-in             zoom_in.png 8 8
gen zoom-out            zoom_out.png 8 8

# ---------------- Status & Busy ----------------
gen clock               clock.png 9 12
gen watch               clock.png 9 12
gen wait                clock.png 9 12
gen progress            clock.png 9 12
gen left_ptr_watch      clock.png 9 12
gen half-busy           clock.png 9 12
gen blocked             blocked.png 9 9
gen crossed_circle      blocked.png 9 9

# ---------------- Help & Info ----------------
gen help                help.png 0 0
gen question_arrow      help.png 0 0
gen coffee_mug          coffee_mug.png 9 9

# ---------------- Drag & Drop ----------------
gen dnd-copy            dnd-anything.png 0 0
gen dnd-move            dnd-anything.png 0 0
gen dnd-link            dnd-anything.png 0 0
gen dnd-none            dnd-anything-blocked.png 0 0
gen dnd-no-drop         dnd-anything-blocked.png 0 0

# ---------------- Resize: Sides (N, S, E, W) ----------------
gen top_side            top_side.png 9 0
gen n-resize            top_side.png 9 0
gen bottom_side         bottom_side.png 9 16
gen s-resize            bottom_side.png 9 16
gen left_side           left_side.png 0 9
gen w-resize            left_side.png 0 9
gen right_side          right_side.png 16 9
gen e-resize            right_side.png 16 9

# ---------------- Resize: Bi-directional & Split ----------------
gen h_double_arrow      left_right_side.png 12 9
gen ew-resize           left_right_side.png 12 9
gen col-resize          left_right_side.png 12 9
gen split_h             left_right_side.png 12 9

gen v_double_arrow      top_bottom_side.png 9 12
gen ns-resize           top_bottom_side.png 9 12
gen row-resize          top_bottom_side.png 9 12
gen split_v             top_bottom_side.png 9 12

# ---------------- Resize: Corners & Diagonals ----------------
gen top_left_corner     top_left_corner.png 0 0
gen nw-resize           top_left_corner.png 0 0
gen ul_angle            top_left_corner.png 2 2

gen top_right_corner    top_right_corner.png 16 0
gen ne-resize           top_right_corner.png 16 0
gen ur_angle            top_right_corner.png 16 2

gen bottom_left_corner  bottom_left_corner.png 0 16
gen sw-resize           bottom_left_corner.png 0 16
gen ll_angle            bottom_left_corner.png 2 16

gen bottom_right_corner bottom_right_corner.png 16 16
gen se-resize           bottom_right_corner.png 16 16
gen lr_angle            bottom_right_corner.png 16 16

# Diagonais duplas (X11 / CSS)
gen bd_double_arrow     top_left_bottom_right_corners.png 12 9
gen nwse-resize         top_left_bottom_right_corners.png 12 9

gen fd_double_arrow     top_right_bottom_left_corners.png 12 9
gen nesw-resize         top_right_bottom_left_corners.png 12 9

# ---------------- Others ----------------
gen fleur               fleur.png 9 9
gen all-scroll          fleur.png 9 9
gen move                fleur.png 9 9
gen top_tee             top_side.png 9 2
gen bottom_tee          bottom_side.png 9 16
gen left_tee            left_side.png 2 9
gen right_tee           right_side.png 16 9

# ---------------- Theme metadata ----------------
echo "[*] Writing index.theme..."

cat > "$BUILD/index.theme" <<EOF
[Icon Theme]
Name=Yekara Cursors
Comment=CRT-optimized cursor theme
Inherits=Default
EOF

echo "[*] Done."

echo
echo "Install with:"
echo "mkdir -p ~/.icons"
echo "cp -r build/$THEME ~/.icons/"