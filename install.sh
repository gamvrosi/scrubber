#!/bin/bash
# Copy example initramfs scripts to their proper places
cp /usr/share/doc/kernel-package/examples/etc/kernel/postinst.d/initramfs /etc/kernel/postinst.d/initramfs
mkdir -p /etc/kernel/postrm.d
cp /usr/share/doc/kernel-package/examples/etc/kernel/postrm.d/initramfs /etc/kernel/postrm.d/initramfs

dpkg -i linux-headers-2.6.35.11-scrub_2.6.35.11-scrub-10.00.Custom_amd64.deb
dpkg -i linux-image-2.6.35.11-scrub_2.6.35.11-scrub-10.00.Custom_amd64.deb
update-grub
