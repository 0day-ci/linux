# SPDX-License-Identifier: GPL-2.0

TOP_DIR="$(realpath "$(dirname "$0")"/../../../../)"

QEMU=qemu-system-x86_64
KERNEL="$TOP_DIR"/arch/x86/boot/bzImage
INITRD=initrd

gen_cpio_list() {
  echo "file /init init 755 0 0" > cpio_list
  echo "file /test $1 755 0 0" >> cpio_list
  echo "dir /dev 755 0 0" >> cpio_list
  echo "nod /dev/console 644 0 0 c 5 1" >> cpio_list
  echo "nod /dev/fd0 666 0 0 b 2 0" >> cpio_list
  echo "dir /mnt 755 0 0" >> cpio_list
}

gen_initrd() {
  "$TOP_DIR"/usr/gen_initramfs.sh -o initrd ./cpio_list
}

get_selftest_log() {
  perl -ne '$begin = $_ =~ /^TAP version/ unless $begin;
            if ($begin && !$end) {
              $_ =~ s/^\s+|\s+$|\[.+//g;
              print($_ . "\n") if $_;
            }
            $end = $_ =~ /^# Totals:/ unless $end;'
}

run_qemu() {
  $QEMU -enable-kvm -nodefaults -nographic \
    -kernel "$KERNEL" -initrd "$INITRD" \
    -append "console=ttyS0 earlyprintk=serial" \
    -serial stdio -monitor none -no-reboot "$@"
}

run_qemu_debug() {
  run_qemu "$@"
}

run_qemu_nodebug() {
  run_qemu "$@" | get_selftest_log
}

detect_debug() {
  if [ "x$1" = "x" ]; then
    run=run_qemu_nodebug
  else
    run=run_qemu_debug
  fi
}

run_qemu_empty() {
  detect_debug "$1"
  $run -drive index=0,if=floppy
}

run_qemu_rdonly_fat() {
  detect_debug "$2"
  $run -drive file=fat:floppy:"$1",index=0,if=floppy,readonly
}

