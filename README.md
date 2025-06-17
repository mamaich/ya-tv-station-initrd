Исходные тексты утилит, которые я использую для Яндекс ТВ Станции.
Подробности будут на https://dzen.ru/mamaich

myinit.c - замена /init в initrd (чтобы он запустился - добавить rdinit=/myinit в командную строку ядра)  
mysudo.c - простой аналог sudo, через сокет /dev/myinit взаимодействиует с myinit чтобы запустить команду или sh с правами root  

musl-gcc -static -s ./myinit.c  -o myinit  
musl-gcc -static -DDEBUG -s ./myinit.c  -o myinit-dbg  
musl-gcc -static -s ./mysudo.c  -o mysudo  
