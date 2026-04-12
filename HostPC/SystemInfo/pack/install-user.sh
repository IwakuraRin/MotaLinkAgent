#!/bin/sh
# Install SystemInfo into ~/.local (no root).
# Run from project root or from extracted portable bundle (script next to ./systeminfo).
set -e
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
# When run as pack/install-user.sh, bundle root is parent; when run as ./install-user.sh in tarball, root is here.
if test -x "$SCRIPT_DIR/systeminfo"; then
	ROOT=$SCRIPT_DIR
elif test -x "$SCRIPT_DIR/../systeminfo"; then
	ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
else
	echo "请在 HostPC/SystemInfo 目录执行: sh pack/install-user.sh" >&2
	echo "或解压便携包后在包目录执行: ./install-user.sh" >&2
	exit 1
fi

BIN="$HOME/.local/bin"
APP="$HOME/.local/share/applications"
ICON_DIR="$HOME/.local/share/icons/hicolor/scalable/apps"
SVG="$ROOT/data/systeminfo.svg"
DESKTOP_SRC="$ROOT/data/com.omniroam.systeminfo.desktop"
EXE="$ROOT/systeminfo"

if ! test -f "$SVG"; then
	echo "找不到图标: $SVG" >&2
	exit 1
fi

mkdir -p "$BIN" "$APP" "$ICON_DIR"
install -m 755 "$EXE" "$BIN/systeminfo"
install -m 644 "$SVG" "$ICON_DIR/systeminfo.svg"
install -m 644 "$DESKTOP_SRC" "$APP/com.omniroam.systeminfo.desktop"
gtk-update-icon-cache -f "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

DESKTOP_DIR=$(xdg-user-dir DESKTOP 2>/dev/null || true)
if test -z "$DESKTOP_DIR" || ! test -d "$DESKTOP_DIR"; then
	DESKTOP_DIR="$HOME/Desktop"
fi
mkdir -p "$DESKTOP_DIR"

DESKTOP_FILE="$DESKTOP_DIR/systeminfo.desktop"
{
	echo "[Desktop Entry]"
	echo "Version=1.0"
	echo "Type=Application"
	echo "Name=SystemInfo"
	echo "Name[zh_CN]=系统信息"
	echo "Comment=OmniRoam system overview and resource usage"
	echo "Exec=$BIN/systeminfo"
	echo "Icon=$ICON_DIR/systeminfo.svg"
	echo "Terminal=false"
	echo "Categories=System;Monitor;GTK;"
	echo "StartupNotify=true"
	echo "StartupWMClass=systeminfo"
} >"$DESKTOP_FILE"
chmod 755 "$DESKTOP_FILE"

echo "已安装到 ~/.local/bin ，并在桌面写入: $DESKTOP_FILE"
echo "若开始菜单未出现，可执行: update-desktop-database ~/.local/share/applications 2>/dev/null || true"
