make
cp -f bin/MonkeyOS iso_root/boot/MonkeyOS

# Create the bootable ISO.
xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
        -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        iso_root -o image.iso

# Install Limine stage 1 and 2 for legacy BIOS boot.
./limine-binary/limine-binary/limine bios-install image.iso

sudo qemu-system-x86_64 -no-reboot -d int,cpu_reset -D qemu.log -enable-kvm -m 512 -drive format=raw,file=image.iso