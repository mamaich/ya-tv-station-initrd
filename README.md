Исходные тексты утилит, которые я использую для Яндекс ТВ Станции.
Подробности будут на https://dzen.ru/mamaich

myinit.c - замена /init в initrd (чтобы он запустился - добавить rdinit=/myinit в командную строку ядра)  
mysudo.c - простой аналог sudo, через сокет /dev/myinit-socket взаимодействует с myinit чтобы запустить команду или sh с правами root  

.../musl-arm/bin/musl-gcc -static -s ./mysudo.c  -o mysudo  
.../musl-arm/bin/musl-gcc -static -s ./myinit.c  -o myinit  
.../musl-arm/bin/musl-gcc -static -DDEBUG -s ./myinit.c  -o myinit-dbg  

