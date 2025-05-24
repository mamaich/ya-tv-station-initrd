Исходные тексты утилит, которые я использую для Яндекс ТВ Станции.
Подробности будут на https://dzen.ru/mamaich

myinit.c - замена /init в initrd (чтобы он запустился - добавить rdinit=/myinit в командную строку ядра)

Остальные файлы не относятся к initrd, но полезны для получения рута, реверсинга/патчинга


/home/mamaich/Android/android-ndk-r21e/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi30-clang -static -I. -L. -Os -g0 -Wl,--wrap=_Z21__libc_init_AT_SECUREPPc ./myinit.c -o ./myinit