#!/bin/sh
# ConfigServer robustness driver: prove the server survives an uncreatable
# config directory instead of crashing in ensure_domain_config (it used to hit
# release_value_but_fixme_should_propagate_errors on the first domain open,
# taking every config client in the session down with it).
#
# XDG_CONFIG_HOME is poisoned with a path *under a regular file*, so
# Directory::create fails with ENOTDIR and ConfigFile::open_for_app errors for
# every domain. The session runs with a minimal non-GUI config client as the
# app; the assertions are that its read/write round-trip degrades to fallback
# values (CONFIG_ROBUSTNESS_OK in the app log) AND that a second client run
# afterwards can still connect and get answers -- i.e. the server is alive.
#
# Usage: configserver-robustness.sh <launcher> <res> <screenshot> <logdir> \
#                                  <window-server> <config> <client>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; cfg="$6"; client="$7"

base=$(mktemp -d /tmp/serenade-configserver-robustness.XXXXXX)
cleanup() {
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

# Poison the config directory: mkdir -p of $base/poison/xdg/sub fails with
# ENOTDIR because $base/poison/xdg is a regular file. StandardPaths prefers
# XDG_CONFIG_HOME over $HOME/.config, so ConfigServer's open_for_app hits this
# for every domain it is asked about.
mkdir -p "$base/poison"
touch "$base/poison/xdg"
export XDG_CONFIG_HOME="$base/poison/xdg/sub"

# Drop state from previous runs: the pid file must be the one *this* launcher
# writes, and stale logs must not masquerade as this run's diagnostics.
rm -f "$shot" "$logdir/serenade-windowserver.pid" "$logdir"/serenade-*.log

dump_logs() {
    echo "configserver-robustness: session logs:"
    for f in "$logdir"/serenade-app.log "$logdir"/serenade-service-*.log; do
        [ -f "$f" ] || continue
        echo "--- $f ---"
        cat "$f"
    done
}

# Long delay + explicit teardown: the assertions drive the run, not the
# launcher's settle timer. The delay must outlive every polling deadline below
# so the launcher never tears services down mid-assertion.
"$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 60000 \
    --log-dir "$logdir" \
    --service /tmp/session/0/portal/config="$cfg" \
    "$ws" "$client" &
launcher_pid=$!

# Wait for the launcher to record WindowServer's pid (written once WS is up).
i=0
while [ ! -s "$logdir/serenade-windowserver.pid" ] && kill -0 "$launcher_pid" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -ge 100 ] && break
    sleep 0.2
done
if [ ! -s "$logdir/serenade-windowserver.pid" ]; then
    echo "configserver-robustness: WindowServer did not start (launcher may have died)"
    dump_logs
    kill -TERM "$launcher_pid" 2>/dev/null || true
    exit 1
fi

# Wait for the in-session client (the launcher's "app") to finish its
# round-trip: it prints CONFIG_ROBUSTNESS_OK on success or a
# "config-client-test:" line on failure. Bounded well under the launcher delay
# so a wedged client cannot race the teardown.
i=0
while ! grep -q "CONFIG_ROBUSTNESS_OK\|config-client-test:" "$logdir/serenade-app.log" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -ge 75 ] && break
    kill -0 "$launcher_pid" 2>/dev/null || break
    sleep 0.2
done

# Second client run: only succeeds if ConfigServer survived the poisoned
# domain opens and still answers (pre-fix it was dead after the first read).
# Watchdog it: a server that dropped or died mid-request makes the synchronous
# call hang forever, so a bounded wait doubles as the liveness assertion.
second_run_failed=0
second_done=0
"$client" &
second_pid=$!
i=0
while [ $i -lt 50 ]; do
    i=$((i + 1))
    if ! kill -0 "$second_pid" 2>/dev/null; then
        wait "$second_pid" || second_run_failed=1
        second_done=1
        break
    fi
    sleep 0.2
done
if [ "$second_done" != 1 ]; then
    echo "configserver-robustness: second client hung waiting for a response (server did not answer)"
    kill -KILL "$second_pid" 2>/dev/null || true
    wait "$second_pid" 2>/dev/null || true
    second_run_failed=1
fi

kill -TERM "$launcher_pid" 2>/dev/null || true
wait "$launcher_pid" 2>/dev/null || true

if [ "$second_run_failed" != 0 ]; then
    echo "configserver-robustness: ConfigServer did not survive the uncreatable config dir (second client could not connect)"
    dump_logs
    exit 1
fi

if ! grep -q "CONFIG_ROBUSTNESS_OK" "$logdir/serenade-app.log"; then
    echo "configserver-robustness: in-session client did not report CONFIG_ROBUSTNESS_OK"
    dump_logs
    exit 1
fi

echo "configserver-robustness: OK, server survived and clients got fallback values"
