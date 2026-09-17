#!/bin/sh
# App-completion driver: prove Maps renders its window on host.
#
# Brings up WindowServer + Maps with Config/Clipboard/Launch as regular
# services and RequestServer as a broker service (it takes over an *accepted*
# client socket, one process per connection -- the same model as WebContent;
# passing it as a regular --service makes it read from its own listener and
# crash in recvmsg with EINVAL). Map tiles load through RequestServer over
# HTTPS; without network or CA certificates the map area shows its "Failed to
# fetch map tiles" fallback, so the assertion is functional
# (--expect-window), not a pixel golden: the window chrome (toolbar, search
# and favorites panels) renders in either case.
#
# Best effort: seed $HOME/.config/certs.pem from the host's CA bundle so that
# on networked hosts RequestServer can verify TLS and real tiles show.
#
# Usage: maps-render.sh <launcher> <res> <screenshot> <logdir> \
#                       <window-server> <maps> <config> <clipboard> <launch> <request>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; maps="$6"; cfg="$7"; clip="$8"; launchsrv="$9"
shift 9
request="$1"

base=$(mktemp -d /tmp/serenade-maps-render.XXXXXX)
cleanup() {
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

mkdir -p "$base/home/.config" "$base/home/.local/share" "$base/runtime"

# Pin the XDG dirs to the session home: StandardPaths prefers them over $HOME,
# and an inherited value (CI images set them) would break config lookups.
export XDG_RUNTIME_DIR="$base/runtime"
export XDG_CONFIG_HOME="$base/home/.config"
export XDG_DATA_HOME="$base/home/.local/share"

# Best effort: give the session a CA bundle for RequestServer's TLS (on hosts
# it reads $HOME/.config/certs.pem). Without one, Maps still renders -- its
# map area just shows the no-tiles fallback.
for ca in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/cacert.pem /etc/ssl/cert.pem /etc/cacert.pem; do
    if [ -f "$ca" ]; then
        cp "$ca" "$base/home/.config/certs.pem"
        break
    fi
done

# Drop state from previous runs so the probe only ever sees pixels (and a pid)
# from this session, and stale logs never masquerade as this run's diagnostics.
rm -f "$shot" "$logdir/serenade-windowserver.pid" "$logdir"/serenade-*.log

exec "$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 6000 \
    --log-dir "$logdir" \
    --home "$base/home" \
    --service /tmp/session/0/portal/config="$cfg" \
    --service /tmp/session/0/portal/clipboard="$clip" \
    --service /tmp/session/0/portal/launch="$launchsrv" \
    --broker-service /tmp/session/0/portal/request="$request" \
    --expect-window 0.30 \
    "$ws" "$maps"
