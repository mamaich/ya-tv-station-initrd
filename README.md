Исходные тексты утилит, которые я использую для Яндекс ТВ Станции.
Подробности будут на https://dzen.ru/mamaich

myinit.c - замена /init в initrd (чтобы он запустился - добавить rdinit=/myinit в командную строку ядра)
send_command.c - отправка команды на сокет, который слушает myinit для ее запуска
su-my-init.c - аналог команды su, использующий взаимодействие с myinit через сокет для запуска интерактивного sh с правами рута, либо для запуска отдельных команд под рутом.

Компилировать:
/home/mamaich/Android/android-ndk-r21e/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi30-clang -static -I. -L. -Os -g0 -Wl,--wrap=_Z21__libc_init_AT_SECUREPPc ./myinit.c -o ./myinit
--wrap нужен чтобы программа при старте не валилась в функции __libc_init_AT_SECUREPP из-за недоступности selinux.
