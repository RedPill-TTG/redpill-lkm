#!/bin/bash
# Usage: always_serial.sh <VM_ID> <SERIAL#>
#
# Runs Proxmox serial console. If it fails it tries again... and again... and again until it succeeds. This is very
# useful when stopping and starting VMs with multiple serial ports. Without it every time you stop and start a VM you
# have to go and open all serial consoles again manually.
# To exit this script press Control+C twice (or if console is active Control+O and then Control+C twice).

trap_cancel() {
    echo "Press Control+C once more terminate the process (or wait 2s for it to restart)"
    sleep 2 || exit 1
}
trap trap_cancel SIGINT SIGTERM

if [[ "$#" -ne 2 ]]; then
  echo "Usage: $0 <VM_ID> <SERIAL_0-3>"
  exit 2
fi

while true; do
 clear
 echo "Started serial$2 monitor for VM=$1 at" $(date)
 qm terminal $1 -iface serial$2
done