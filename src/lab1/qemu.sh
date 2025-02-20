qemu-system-riscv64 \
    -nographic \
    -machine virt \
    -kernel arch/riscv/boot/Image \
    -device virtio-blk-device,drive=hd0 \
    -append "root=/dev/vda ro console=ttyS0" \
    -bios default \
    -drive file=../lab0/rootfs.img,format=raw,id=hd0,if=none \
    -S -s
