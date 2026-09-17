#!/bin/sh
# App-completion driver: prove TextEditor opens a real file on host.
#
# Launches TextEditor with a fixed test document as its CLI argument. The file
# goes through request_file_read_only_approved(), which auto-approves (no
# prompt), so this is fully headless: the assertion is that the document's
# text is visible in the screenshot, i.e. the full path worked: TextEditor ->
# FileSystemAccessServer IPC -> file opened -> rendered in the editor.
#
# The web view used for markdown/HTML preview is created lazily and a plain
# .txt triggers no preview, so no WebContent service is needed. Only
# Config/Clipboard/Launch (regular) and FileSystemAccessServer (broker -- it
# takes over an *accepted* client socket, one process per connection) run.
#
# TextEditor restores/saves its window size+position from config, so a fresh
# per-run $HOME keeps the placement deterministic: always the default 640x400
# at WindowServer's fixed initial position. The content is fixed text in a
# fixed font, so the render is golden-able (the blinking cursor can flip
# between runs, but it is far below the compare tolerance).
#
# Usage: texteditor-open.sh <launcher> <res> <screenshot> <logdir> <golden> \
#                           <window-server> <text-editor> <config> <clipboard> \
#                           <launch> <filesystemaccess>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"; golden="$5"
ws="$6"; te="$7"; cfg="$8"; clip="$9"; launch="${10}"
shift 10
fas="$1"

base=/tmp/serenade-texteditor
home="$base/home"
doc="$base/doc.txt"

rm -rf "$base"
mkdir -p "$home/.config" "$home/.local/share" "$base/runtime" "$logdir"

# Pin the XDG dirs to the session home: StandardPaths prefers them over $HOME,
# and an inherited value (CI images set them) would break config lookups.
export XDG_RUNTIME_DIR="$base/runtime"
export XDG_CONFIG_HOME="$home/.config"
export XDG_DATA_HOME="$home/.local/share"

# Fixed content: no timestamps or host-dependent data, so the golden is stable.
cat > "$doc" <<'EOF'
SerenaDE TextEditor smoke document.
Line two of the fixed content.
Line three keeps the golden stable.
EOF

# Drop state from previous runs so the probe only ever sees pixels (and a pid)
# from this session, and stale logs never masquerade as this run's diagnostics.
rm -f "$shot" "$logdir/serenade-windowserver.pid" "$logdir"/serenade-*.log

exec "$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 6000 \
    --log-dir "$logdir" \
    --home "$home" \
    --service /tmp/session/0/portal/config="$cfg" \
    --service /tmp/session/0/portal/clipboard="$clip" \
    --service /tmp/session/0/portal/launch="$launch" \
    --broker-service /tmp/session/0/portal/filesystemaccess="$fas" \
    --golden "$golden" \
    "$ws" "$te" "$doc"
