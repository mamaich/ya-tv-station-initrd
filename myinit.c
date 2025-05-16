#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>

// Функция для создания и монтирования proc
int mount_proc(const char *mountpoint, const char *context) {
    struct stat st;
    if (stat(mountpoint, &st) < 0) {
        if (mkdir(mountpoint, 0555) < 0) {
            fprintf(stderr, "myinit: Failed to create %s directory (%s): %s\n", mountpoint, context, strerror(errno));
            return 1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "myinit: %s exists but is not a directory (%s)\n", mountpoint, context);
        return 1;
    }

    if (mount("proc", mountpoint, "proc", 0, NULL) < 0) {
        fprintf(stderr, "myinit: Failed to mount %s (%s): %s\n", mountpoint, context, strerror(errno));
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // Проверяем, что программа запущена с правами root
    if (geteuid() != 0) {
        fprintf(stderr, "myinit: This program must be run as root\n");
        return 1;
    }

    // Проверяем параметры командной строки
    if (argc == 1) {
        // Нет параметров: создаём дочерний процесс и запускаем /init
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "myinit: Failed to fork: %s\n", strerror(errno));
            return 1;
        } else if (pid == 0) {
            // Дочерний процесс: запускаем копию программы с параметром "1"
            if (execl(argv[0], argv[0], "1", NULL) < 0) {
                fprintf(stderr, "myinit: Failed to exec %s with arg 1: %s\n", argv[0], strerror(errno));
                return 1;
            }
        } else {
            // ждем 1 секунду, чтобы наша дочка сделала свои дела при старте
            sleep(1);
            // Родительский процесс: запускаем /init
            if (execl("/init", "/init", NULL) < 0) {
                fprintf(stderr, "myinit: Failed to exec /init: %s\n", strerror(errno));
                return 1;
            }
        }
        return 0; // Не достигается, так как execl заменяет процесс
    }

    // Если передан параметр "1", выполняем текущую логику
    if (argc >= 2 && strcmp(argv[1], "1") == 0) {
        puts("myinit: Waiting for /data mount");

        while(1) {
            sleep(1);

            if (kill(1, SIGSTOP) == -1) {
                perror("myinit: kill SIGSTOP");
                continue;
            }

//            puts("myinit: Mounting proc filesystem to /first_stage_ramdisk");
            // Монтируем /first_stage_ramdisk для доступа к /first_stage_ramdisk/1/mounts
            if (mount_proc("/first_stage_ramdisk", "waiting for /data")) {
                kill(1, SIGCONT);
                continue;
            }

            FILE *proc_mounts = fopen("/first_stage_ramdisk/1/mounts", "r");
            if (!proc_mounts) {
                fprintf(stderr, "myinit: Failed to open /first_stage_ramdisk/1/mounts: %s\n", strerror(errno));
                umount2("/first_stage_ramdisk", 0);
                kill(1, SIGCONT);
                continue;
            }

//            puts("myinit: /init mounts:");
            char line[256];
            int data_mounted=0;
            while (fgets(line, sizeof(line), proc_mounts)) {
                line[strcspn(line, "\n")] = '\0';
//                printf("myinit: %s\n", line);
                if(strstr(line," /data "))
                {
                    data_mounted = 1;
                    break;
                }
            }

            fclose(proc_mounts);

            // Размонтируем /first_stage_ramdisk
            if (umount2("/first_stage_ramdisk", 0) < 0) {
                fprintf(stderr, "myinit: Failed to unmount /first_stage_ramdisk: %s\n", strerror(errno));
            }

            // Возобновляем процесс
            if (kill(1, SIGCONT) == -1) {
                perror("myinit: kill SIGCONT");
            }

            if(data_mounted)
                break;
        }

        puts("myinit: /data mounted, attaching to /init namespace");
        if (mount_proc("/first_stage_ramdisk", "before setns")) {
            return 1;
        }

        // Открываем файловый дескриптор для пространства имён /init (PID 1)
        int ns_fd = open("/first_stage_ramdisk/1/ns/mnt", O_RDONLY);
        if (ns_fd < 0) {
            fprintf(stderr, "myinit: Failed to open /first_stage_ramdisk/1/ns/mnt: %s\n", strerror(errno));
            umount2("/first_stage_ramdisk", 0);
            return 1;
        }

        // Размонтируем /first_stage_ramdisk до setns
        if (umount2("/first_stage_ramdisk", 0) < 0) {
            fprintf(stderr, "myinit: Failed to unmount /first_stage_ramdisk (before setns): %s\n", strerror(errno));
            close(ns_fd);
            return 1;
        }

        // Присоединяемся к пространству имён монтирований
        if (setns(ns_fd, CLONE_NEWNS) < 0) {
            fprintf(stderr, "myinit: Failed to setns to /init mount namespace: %s\n", strerror(errno));
            close(ns_fd);
            return 1;
        }
        close(ns_fd);

        puts("myinit: Starting /data/local/tmp/myscript.sh");
        system("sh /data/local/tmp/myscript.sh");

        puts("myinit: Starting interactive shell on console");
        // Запускаем /system/bin/sh
        if (execl("/system/bin/sh", "sh", NULL) < 0) {
            fprintf(stderr, "Failed to exec /system/bin/sh: %s\n", strerror(errno));
            return 1;
        }
    } else {
        // Неверный параметр командной строки
        fprintf(stderr, "Invalid argument. Use '1' as the argument.\n");
        return 1;
    }

    return 0; // Не достигается, так как execl заменяет процесс
}
