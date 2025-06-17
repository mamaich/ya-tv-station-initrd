Исходные тексты утилит, которые я использую для Яндекс ТВ Станции.
Подробности будут на https://dzen.ru/mamaich

myinit.c - замена /init в initrd (чтобы он запустился - добавить rdinit=/myinit в командную строку ядра)
su-my-init.c - простой аналог su, через сокет /dev/myinit взаимодействиует с myinit чтобы запустить команду или sh с правами root.

/home/mamaich/musl-arm/bin/musl-gcc -static -s ./myinit.c  -o myinit  
/home/mamaich/musl-arm/bin/musl-gcc -static -DDEBUG -s ./myinit.c  -o myinit-dbg  

sudo find . | cpio -o -H newc >../myinitrd.cpio