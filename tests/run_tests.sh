#!/bin/bash
# Run inside the Ubuntu VM, as root, from anywhere:
#   sudo tests/run_tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SECONDS_AT_RATE="${SECONDS_AT_RATE:-10}"

if [[ "$(id -u)" -ne 0 ]]; then
	echo "run as root in the VM: sudo tests/run_tests.sh" >&2
	exit 1
fi

gcc -Wall -Wextra -Werror -O2 -g -pthread -Iinclude -Iuser \
	user/queue_test.c user/queue.c -o user/queue_test
gcc -Wall -Wextra -Werror -O2 -g -pthread -Iinclude -Iuser \
	user/reader.c user/queue.c -o user/reader
gcc -Wall -Wextra -Werror -O2 -g -Iinclude user/telemetryctl.c -o user/telemetryctl
gcc -Wall -Wextra -Werror -O2 -g -Iinclude tests/errno_test.c -o tests/errno_test
gcc -Wall -Wextra -Werror -O2 -g tests/poll_test.c -o tests/poll_test
make -C kernel

echo "== queue =="
./user/queue_test

echo "== load/unload x50 =="
dmesg -C || true
for _ in $(seq 1 50); do
	insmod kernel/telemetry.ko
	test -c /dev/telemetry
	rmmod telemetry
done

if insmod kernel/telemetry.ko rate_hz=0; then
	echo "rate_hz=0 was accepted" >&2
	exit 1
fi

insmod kernel/telemetry.ko
echo "== errno and poll =="
./tests/errno_test
./tests/poll_test | tee /tmp/poll.out
grep -q 'readable=1' /tmp/poll.out

run_reader() {
	local out="$1"
	./user/reader >"$out" 2>&1 &
	local pid=$!
	sleep "$SECONDS_AT_RATE"
	kill -INT "$pid"
	wait "$pid" || true
	grep -q 'gaps=0' "$out"
	python3 tests/check_seq.py samples.csv
	mv samples.csv "$out.csv"
}

echo "== contiguous seq at 100 Hz =="
run_reader /tmp/reader100.out

echo "== contiguous seq at 1 kHz =="
./user/telemetryctl set-rate 1000
run_reader /tmp/reader1k.out

echo "== rmmod while open fails =="
./user/reader >/tmp/reader-open.out 2>&1 &
pid=$!
sleep 1
if rmmod telemetry; then
	echo "rmmod succeeded while the device was open" >&2
	kill -INT "$pid" || true
	wait "$pid" || true
	exit 1
fi
kill -INT "$pid"
wait "$pid" || true

echo "== slow writer shows gaps =="
TELEMETRY_WRITER_US=200000 ./user/reader >/tmp/reader-slow.out 2>&1 &
pid=$!
sleep "$SECONDS_AT_RATE"
kill -INT "$pid"
wait "$pid" || true
python3 - <<'PY'
import re
text = open("/tmp/reader-slow.out").read()
match = re.search(r"gaps=(\d+)", text)
gaps = int(match.group(1)) if match else -1
dropped = None
for line in open("/proc/telemetry_stats"):
    if line.startswith("dropped:"):
        dropped = int(line.split()[1])
print(f"gaps={gaps} dropped={dropped}")
# Drop-newest keeps the oldest samples, and those have contiguous seq.
# A short run therefore drops thousands and still writes gaps=0.
# The holes show up only after the reader has consumed the whole ring.
if dropped is None or dropped <= 0:
    raise SystemExit(1)
PY

rmmod telemetry
if dmesg | grep -E 'BUG:|Oops:|cut here'; then
	echo "kernel log contains an error" >&2
	exit 1
fi
echo "all tests ok"
