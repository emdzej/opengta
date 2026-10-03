#!/bin/bash
# Launcher of OpenGTA (gasm).app: OpenGTA @VERSION@ (opengta.wasm) on the bundled gasm-run @GASM_VERSION@.
# Installed as Contents/MacOS/OpenGTA by tools/package-gasm.sh. macOS ships bash 3.2: no bash 4 features.
#
# Finds the GTA game data (remembered in ~/Library/Application Support/OpenGTA/data-location), asks for it
# with a dialog when it is missing, then runs gasm-run --asset-dir <data>; its output goes to
# ~/Library/Logs/OpenGTA/gasm.log.
# Test hooks (no dialogs): OPENGTA_DATA=<folder> uses that data without saving it (and turns missing data into
# exit 2), OPENGTA_DRY_RUN=1 or --dry-run prints the gasm-run command instead of running it. --help: the options.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$(cd "$HERE/../Resources" && pwd)"
CONF="$HOME/Library/Application Support/OpenGTA"
LOCFILE="$CONF/data-location"
LOGDIR="$HOME/Library/Logs/OpenGTA"
LOG="$LOGDIR/gasm.log"
TITLE="OpenGTA (gasm)"
EXE_SIZE=774144   # WINO/Grand Theft Auto.exe of Rockstar's 2002 re-release
DRY=${OPENGTA_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
OpenGTA (gasm).app: OpenGTA @VERSION@ on gasm-run @GASM_VERSION@

  open "OpenGTA (gasm).app" [--args [options] [DATA] [gasm-run options...]]
  "OpenGTA (gasm).app/Contents/MacOS/OpenGTA" [options] [DATA] [gasm-run options...]

DATA is your copy of GTA from Rockstar's 2002 re-release (GTAINSTALLER.zip): the installed game's folder
(it has GTADATA/ and WINO/Grand Theft Auto.exe) or the unzipped installer (the folder with data1.cab and
data2.cab). It is remembered in
  $LOCFILE
Without one the app asks for it. Hold Option while opening the app (or pass --change-data) to pick
another; --forget-data deletes the saved location.

Options:
  --change-data  ask for the game data even if a location is saved
  --forget-data  delete the saved location and exit
  --dry-run      print the gasm-run command instead of running it (also OPENGTA_DRY_RUN=1)
  --help         this text
Anything after DATA goes to gasm-run, e.g. --param intro=0, --window 1280x960, --filter nearest, --mute.
OPENGTA_DATA=<DATA> uses that data for one run without saving it.

Log: $LOG
More: https://opengta.emdzej.pl/guide/install
EOF
}

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_data <path>: exit 0 if usable; otherwise prints why
check_data() {
  local p=$1 gd wino exe size c1 c2
  if [ -f "$p" ]; then
    case "$(printf '%s' "$p" | tr '[:upper:]' '[:lower:]')" in
      *.zip) echo "This is the zip: unzip GTAINSTALLER.zip first, then choose the folder it makes." ;;
      *) echo "This is a file: choose the GTA folder (or the unzipped installer's folder)."; echo "$p" ;;
    esac
    return 1
  fi
  if [ ! -d "$p" ]; then
    echo "The GTA game data was not found at:"
    echo "$p"
    echo "Choose it again."
    return 1
  fi
  gd=$(ci_find "$p" GTADATA d); wino=$(ci_find "$p" WINO d)
  if [ -n "$gd" ] || [ -n "$wino" ]; then                                   # the installed game
    if [ -z "$gd" ] || [ -z "$(ci_find "$gd" MISSION.INI f)" ]; then
      echo "This GTA folder has no GTADATA/MISSION.INI:"; echo "$p"; return 1
    fi
    exe=""; [ -n "$wino" ] && exe=$(ci_find "$wino" "Grand Theft Auto.exe" f)
    if [ -z "$exe" ]; then echo "This GTA folder has no WINO/Grand Theft Auto.exe:"; echo "$p"; return 1; fi
    size=$(wc -c <"$exe" | tr -d ' ')
    if [ "$size" != "$EXE_SIZE" ]; then
      echo "WINO/Grand Theft Auto.exe is $size bytes, not 774,144: OpenGTA needs the Windows game of Rockstar's 2002 re-release (GTAINSTALLER.zip)."
      echo "$p"; return 1
    fi
    return 0
  fi
  c1=$(ci_find "$p" data1.cab f); c2=$(ci_find "$p" data2.cab f)
  if [ -n "$c1" ] && [ -n "$c2" ]; then return 0; fi                       # the unzipped installer
  if [ -n "$c1" ] || [ -n "$c2" ]; then
    echo "This installer folder needs both data1.cab and data2.cab:"; echo "$p"; return 1
  fi
  echo "This folder is neither the installed GTA (it has GTADATA and WINO) nor the unzipped installer (it has data1.cab and data2.cab):"
  echo "$p"
  return 1
}

# absolute <path>: absolute path without a trailing slash
absolute() {
  local p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

option_held() { # the Option key is down (NSEvent modifier flag 1 << 19)
  [ "$(osascript -l JavaScript -e 'ObjC.import("AppKit"); ($.NSEvent.modifierFlags & 0x80000) ? "1" : "0"' 2>/dev/null)" = 1 ]
}

# ask <message>: a dialog, then a folder chooser; prints the chosen path (empty on Quit)
ask() {
  osascript -e 'on run argv' \
    -e 'display dialog (item 1 of argv) with title (item 2 of argv) buttons {"Quit", "Choose your GTA folder…"} default button 2 cancel button 1 with icon note' \
    -e 'end run' "$1" "$TITLE" >/dev/null 2>&1 || return 0
  osascript -e 'POSIX path of (choose folder with prompt "Choose the installed GTA (the folder with GTADATA and WINO) or the unzipped installer (the folder with data1.cab):")' 2>/dev/null
}

INTRO="OpenGTA needs your copy of GTA (it is not included): Rockstar's 2002 re-release, GTAINSTALLER.zip.

Choose the folder of the installed game (it has GTADATA and WINO), or the folder you unzipped GTAINSTALLER.zip to (it has data1.cab and data2.cab; no need to run its setup).

Your choice is remembered. To change it later, hold Option while opening the app."

# Launcher options, then optional game data, then gasm-run's options.
while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-data) CHANGE=1; shift ;;
    --forget-data) rm -f "$LOCFILE"; echo "forgot the game data location ($LOCFILE)"; exit 0 ;;
    -psn_*) shift ;;   # process serial number from older Finder launches
    *) break ;;
  esac
done
DATAP="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then DATAP=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$DATAP" ] && [ -n "${OPENGTA_DATA:-}" ]; then DATAP=$(absolute "$OPENGTA_DATA"); fi
if [ -z "$DATAP" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then
  DATAP=$(head -n 1 "$LOCFILE")
  if [ "$DRY" = 0 ] && option_held; then DATAP=""; fi
fi

MSG=$INTRO
[ -n "$DATAP" ] && { MSG=$(check_data "$DATAP") || true; }
while [ -z "$DATAP" ] || ! check_data "$DATAP" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENGTA_DATA:-}" ]; then   # never open dialogs in test runs
    echo "no usable GTA game data${DATAP:+: $MSG}" >&2
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

CMD=("$HERE/gasm-run" "$RES/opengta.wasm" --asset-dir "$DATAP" "$@")
if [ "$DRY" = 1 ]; then printf '%q ' "${CMD[@]}"; echo; exit 0; fi

mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenGTA @VERSION@, gasm-run @GASM_VERSION@"; printf '%q ' "${CMD[@]}"; echo; } >>"$LOG"
"${CMD[@]}" >>"$LOG" 2>&1
rc=$?
if [ "$rc" != 0 ] && [ -z "${OPENGTA_DATA:-}" ]; then   # no dialog in test runs
  osascript -e 'on run argv' \
    -e 'display alert "OpenGTA stopped with an error" message (item 1 of argv) as critical' -e 'end run' \
    "$(tail -n 4 "$LOG")

Full log: $LOG" >/dev/null 2>&1
fi
exit "$rc"
