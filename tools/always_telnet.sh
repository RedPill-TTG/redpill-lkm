#!/bin/bash
# Usage: always_telnet.sh <IP> <PORT>
#
# Runs telnet remote console. If it fails it tries again... and again... and again until it succeeds. This is very
# useful when stopping and starting VMs with multiple serial ports on ESXi. Without it every time you stop and start a
# VM you have to go and open all serial consoles again manually.
# To exit this script press Control+C twice (or if console is active Control+O and then Control+C twice).

trap_cancel() {
    echo "Press Control+C once more terminate the process (or wait 2s for it to restart)"
    sleep 2 || exit 1
}
trap trap_cancel SIGINT SIGTERM

if [[ "$#" -ne 2 ]]; then
  echo "Usage: $0 <IP> <PORT>"
  exit 2
fi

while true; do
 clear
 echo "Started telnet for $1:$2 at" $(date)
 telnet $1 $2
 sleep 0.2
done