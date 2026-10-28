file hello.ko # 应显示 ELF 64-bit ... ARM aarch64
modinfo hello.ko # 看 vermagic 字段

sudo insmod hello.ko
lsmod | grep hello # 能看到 hello
dmesg | tail -3 # hello: module loaded, init done
sudo rmmod hello
dmesg | tail -3 # hello: module unloaded, exit done