#!/bin/sh
# OpenGTA @VERSION@ (opengta.wasm) on the bundled gasm-run @GASM_VERSION@: Linux launcher.
#   ./opengta.sh [options] [DATA] [gasm-run options...]          (./opengta.sh --help)
# The game data location comes from the argument (then saved), else $XDG_CONFIG_HOME/opengta/data-location,
# else a zenity or kdialog chooser (when a display is available), else the usage text. Test hooks (no
# dialogs): OPENGTA_DATA=<folder> uses that data without saving it (and turns missing data into exit 2),
# OPENGTA_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/opengta"
LOCFILE="$CONF/data-location"
LOGDIR="${XDG_STATE_HOME:-$HOME/.local/state}/opengta"
LOG="$LOGDIR/gasm.log"
TITLE="OpenGTA (gasm)"
EXE_SIZE=774144   # WINO/Grand Theft Auto.exe of Rockstar's 2002 re-release
DRY=${OPENGTA_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
OpenGTA @VERSION@ on gasm-run @GASM_VERSION@

  $0 [options] [DATA] [gasm-run options...]

DATA is your copy of GTA from Rockstar's 2002 re-release (GTAINSTALLER.zip): the installed game's folder
(it has GTADATA/ and WINO/Grand Theft Auto.exe) or the unzipped installer (the folder with data1.cab and
data2.cab). It is saved in
  $LOCFILE
so later runs need no argument. Without one, a chooser opens (zenity or kdialog).

Options:
  --change-data      ask for the game data even if a location is saved
  --forget-data      delete the saved location and exit
  --install-desktop  add a menu entry (~/.local/share/applications/opengta-gasm.desktop) and exit
  --dry-run          print the gasm-run command instead of running it (also OPENGTA_DRY_RUN=1)
  --help             this text
Anything after DATA goes to gasm-run, e.g. --param intro=0, --window 1280x960, --filter nearest, --mute.
OPENGTA_DATA=<DATA> uses that data for one run without saving it.

More: https://opengta.emdzej.pl/guide/install
EOF
}

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_data <path>: exit 0 if usable; otherwise prints why
check_data() {
  if [ -f "$1" ]; then
    case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
      *.zip) echo "This is the zip: unzip GTAINSTALLER.zip first, then choose the folder it makes." ;;
      *) printf 'This is a file: choose the GTA folder (or the unzipped installer'"'"'s folder).\n%s\n' "$1" ;;
    esac
    return 1
  fi
  if [ ! -d "$1" ]; then
    printf 'The GTA game data was not found at:\n%s\nChoose it again.\n' "$1"
    return 1
  fi
  gd=$(ci_find "$1" GTADATA d); wino=$(ci_find "$1" WINO d)
  if [ -n "$gd" ] || [ -n "$wino" ]; then                                   # the installed game
    if [ -z "$gd" ] || [ -z "$(ci_find "$gd" MISSION.INI f)" ]; then
      printf 'This GTA folder has no GTADATA/MISSION.INI:\n%s\n' "$1"; return 1
    fi
    exe=""; [ -n "$wino" ] && exe=$(ci_find "$wino" "Grand Theft Auto.exe" f)
    if [ -z "$exe" ]; then printf 'This GTA folder has no WINO/Grand Theft Auto.exe:\n%s\n' "$1"; return 1; fi
    size=$(wc -c <"$exe" | tr -d ' ')
    if [ "$size" != "$EXE_SIZE" ]; then
      printf 'WINO/Grand Theft Auto.exe is %s bytes, not 774,144: OpenGTA needs the Windows game of Rockstar'"'"'s 2002 re-release (GTAINSTALLER.zip).\n%s\n' "$size" "$1"
      return 1
    fi
    return 0
  fi
  c1=$(ci_find "$1" data1.cab f); c2=$(ci_find "$1" data2.cab f)
  if [ -n "$c1" ] && [ -n "$c2" ]; then return 0; fi                       # the unzipped installer
  if [ -n "$c1" ] || [ -n "$c2" ]; then
    printf 'This installer folder needs both data1.cab and data2.cab:\n%s\n' "$1"; return 1
  fi
  printf 'This folder is neither the installed GTA (it has GTADATA and WINO) nor the unzipped installer (it has data1.cab and data2.cab):\n%s\n' "$1"
  return 1
}

absolute() { # absolute path without a trailing slash
  p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

have_gui() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && { command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1; }; }

# ask <message>: prints the chosen folder (empty on Quit)
ask() {
  pick="Choose the installed GTA (with GTADATA and WINO) or the unzipped installer (with data1.cab)"
  if command -v zenity >/dev/null 2>&1; then
    zenity --question --title "$TITLE" --text "$1" --ok-label "Choose your GTA folder" --cancel-label Quit 2>/dev/null &&
      zenity --file-selection --directory --title "$pick" 2>/dev/null
  else
    kdialog --title "$TITLE" --yesno "$1" --yes-label "Choose your GTA folder" --no-label Quit 2>/dev/null &&
      kdialog --title "$pick" --getexistingdirectory "$HOME" 2>/dev/null
  fi
}

error_box() {
  if command -v zenity >/dev/null 2>&1; then zenity --error --title "$TITLE" --text "$1" 2>/dev/null
  elif command -v kdialog >/dev/null 2>&1; then kdialog --title "$TITLE" --error "$1" 2>/dev/null; fi
}

install_desktop() {
  dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  mkdir -p "$dir"
  sed -e "s|@DIR@|$HERE|g" "$HERE/opengta-gasm.desktop" >"$dir/opengta-gasm.desktop"
  echo "installed $dir/opengta-gasm.desktop"
}

INTRO="OpenGTA needs your copy of GTA (it is not included): Rockstar's 2002 re-release, GTAINSTALLER.zip.

Choose the folder of the installed game (it has GTADATA and WINO), or the folder you unzipped GTAINSTALLER.zip to (it has data1.cab and data2.cab; no need to run its setup).

Your choice is remembered; run opengta.sh --change-data to pick another."

while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-data) CHANGE=1; shift ;;
    --forget-data) rm -f "$LOCFILE"; echo "forgot the game data location ($LOCFILE)"; exit 0 ;;
    --install-desktop) install_desktop; exit 0 ;;
    *) break ;;
  esac
done
DATAP="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then DATAP=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$DATAP" ] && [ -n "${OPENGTA_DATA:-}" ]; then DATAP=$(absolute "$OPENGTA_DATA"); fi
if [ -z "$DATAP" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then DATAP=$(head -n 1 "$LOCFILE"); fi

MSG=$INTRO
[ -n "$DATAP" ] && { MSG=$(check_data "$DATAP") || true; }
while [ -z "$DATAP" ] || ! check_data "$DATAP" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENGTA_DATA:-}" ] || ! have_gui; then
    [ -n "$DATAP" ] && printf '%s\n\n' "$MSG" >&2
    [ "$DRY" = 1 ] || [ -n "${OPENGTA_DATA:-}" ] || usage >&2
    exit 2
  fi
  DATAP=$(ask "$MSG")
  [ -n "$DATAP" ] || exit 0
  DATAP=$(absolute "$DATAP")
  SAVE=1
  MSG=$(check_data "$DATAP") || true
done
if [ "$SAVE" = 1 ] && [ "$DRY" = 0 ]; then
  mkdir -p "$CONF" && printf '%s\n' "$DATAP" >"$LOCFILE"
fi

set -- "$HERE/gasm-run" "$HERE/opengta.wasm" --asset-dir "$DATAP" "$@"
if [ "$DRY" = 1 ]; then
  for a in "$@"; do printf "'%s' " "$(printf '%s' "$a" | sed "s/'/'\\\\''/g")"; done
  echo
  exit 0
fi

if [ -t 1 ] || [ -t 2 ]; then exec "$@"; fi
# Started from a menu or file manager: keep the output in a log and show errors in a dialog.
mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenGTA @VERSION@, gasm-run @GASM_VERSION@"; echo "$*"; } >>"$LOG"
"$@" >>"$LOG" 2>&1
rc=$?
[ "$rc" = 0 ] || [ -n "${OPENGTA_DATA:-}" ] || error_box "OpenGTA stopped with an error:

$(tail -n 4 "$LOG")

Full log: $LOG"
exit "$rc"
