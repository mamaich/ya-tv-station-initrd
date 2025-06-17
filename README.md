Исходные тексты утилит, которые я использую для Яндекс ТВ Станции.
Подробности будут на https://dzen.ru/mamaich

myinit.c - замена /init в initrd (чтобы он запустился - добавить rdinit=/myinit в командную строку ядра)

Остальные файлы не относятся к initrd, но полезны для получения рута, реверсинга/патчинга


/home/mamaich/musl-arm/bin/musl-gcc -static -s ./myinit.c  -o myinit
/home/mamaich/musl-arm/bin/musl-gcc -static -DDEBUG -s ./myinit.c  -o myinit-dbg
sudo find . | cpio -o -H newc >../magisk.cpio