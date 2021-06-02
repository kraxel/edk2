#!/bin/sh
#
# boot script to test ovmf with microvm.
#
# known issues / todo list items:
#  - microvm has no flash support so using -bios without
#    separate varstore for now.
#  - ovmf needs rtc=on (for ram detection via cmos).
#

DISK="/vmdisk/imagefish/fedora-33-efi-systemd-x86_64.qcow2"
QEMU="/home/kraxel/projects/qemu/build/default/qemu-system-x86_64"
BIOS="Build/MicrovmX64/DEBUG_GCC5/FV/MICROVM.fd"

set -ex
$QEMU \
    -nodefaults \
    -enable-kvm -m 1G -boot menu=on \
    -machine microvm,acpi=on,pit=off,pic=off,rtc=on \
    -global virtio-mmio.force-legacy=false \
    -bios "$BIOS" \
    \
    -display gtk -serial vc \
    -chardev stdio,id=firmware.log \
    -device isa-debugcon,iobase=0x402,chardev=firmware.log \
    \
    -drive if=none,id=disk,format="${DISK##*.}",file="$DISK" \
    -device virtio-scsi-device \
    -device scsi-hd,drive=disk,bootindex=1 \
    \
    "$@"
