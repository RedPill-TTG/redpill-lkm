#!/usr/bin/env bash
# Injects RedPill LKM file into a ramdisk inside of an existing image (so you can test new LKM without constant full image
# rebuild & transfer)
#
# Internally we use it something like this with Proxmox pointing to /dev/loop0 as the USB source:
#   rm redpill.ko ; wget https://buildsrv/redpill.ko ; \
#   IRP_LEAVE_ATTACHED=1 ./inject_rp_ko.sh rp-3615-v6.img redpill.ko ; losetup ; \
#   qm stop 101 ; sleep 1 ; qm start 101 ; qm terminal 101 -iface serial1

self=${0##*/}
img="$(realpath $1 2> /dev/null)"
lkm="$(realpath $2 2> /dev/null)"
if	[ $# -ne 2 -o ! -f "$img" -o ! -f "$lkm" ]
then
	echo "Usage: $self <rp-load-img> <redpill.ko>"
	exit 2
fi

echo "Detaching $img from all loopdevs"
losetup -j "$img" | grep -E -o '^/dev/loop[0-9]+' | \
while read -r loopdev; do
  umount "${loopdev}p"? 2>/dev/null
  losetup -d "$loopdev"
  echo "Detached $loopdev"
done

losetup -j "$img" | grep -E -q '^/dev/loop[0-9]+'
if [ $? -eq 0 ]; then
  echo "$img is still attached to some loop devs!"
  exit 1
fi

set -euo pipefail
LODEV="$(losetup --show -f -P "$img")"

UNIQ_BASE="$PWD/__inject_rp_$(date '+%s')"
echo "Making directories in $UNIQ_BASE"
TMP_MNT_DIR="$UNIQ_BASE/img-mnt"
TMP_RDU_DIR="$UNIQ_BASE/rd-unpacked"
mkdir -p "$TMP_MNT_DIR"
mkdir -p "$TMP_RDU_DIR"

echo "Mounting in $TMP_MNT_DIR"
mount "${LODEV}p1" "$TMP_MNT_DIR"

echo "Unpacking $TMP_MNT_DIR/rd.gz"
cd "$TMP_RDU_DIR"
if file "$TMP_MNT_DIR/rd.gz" | grep -q 'cpio archive'; then # special case: uncompressed rd
  IRP_FLAT_RD=1
  cat "$TMP_MNT_DIR/rd.gz" | cpio -idmv
else
  IRP_FLAT_RD=0
  xz -dc < "$TMP_MNT_DIR/rd.gz" | cpio -idmv
fi

echo "Copying $lkm"
cp "$lkm" "$TMP_RDU_DIR/usr/lib/modules/rp.ko"

echo "Repacking $TMP_MNT_DIR/rd.gz"
if [[ IRP_FLAT_RD -eq 1 ]]; then # special case: uncompressed rd
  find . 2>/dev/null | cpio -o -H newc -R root:root > "$TMP_MNT_DIR/rd.gz"
else
  find . 2>/dev/null | cpio -o -H newc -R root:root | xz -9 --format=lzma > "$TMP_MNT_DIR/rd.gz"
fi

echo "Unmounting & detaching (if requested)"
sync
umount "$TMP_MNT_DIR"
if [[ -z "${IRP_LEAVE_ATTACHED}" ]]; then
  losetup -d "$LODEV"
fi

echo "Cleaning up $UNIQ_BASE"
rm -rf "$UNIQ_BASE"