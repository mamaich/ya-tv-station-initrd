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
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

// Заглушка для __libc_init_AT_SECURE
void __wrap__Z21__libc_init_AT_SECUREPPc(char** args) { }

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

// Функция для обработки клиентского подключения
void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

#ifdef DEBUG
    puts("myinit: Received new client connection");
#endif

    // Читаем команду из сокета (формат: PID<SOH>command)
    char buffer[1024];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        fprintf(stderr, "myinit: Failed to read from client socket: %s\n", strerror(errno));
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';
#ifdef DEBUG
    fprintf(stderr, "myinit: Received message (%zd bytes): '%s'\n", bytes_read, buffer);
#endif

    // Парсим PID и команду, используя SOH (\001) как разделитель
    char *pid_str = strtok(buffer, "\001");
    char *command = strtok(NULL, "");
    if (!pid_str || !command) {
        fprintf(stderr, "myinit: Invalid message format, expected PID<SOH>command, got pid_str='%s', command='%s'\n",
                pid_str ? pid_str : "(null)", command ? command : "(null)");
        close(client_fd);
        return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "myinit: Parsed PID='%s', command='%s'\n", pid_str, command);
#endif

    // Преобразуем PID в число
    pid_t client_pid = atoi(pid_str);
    if (client_pid <= 0) {
        fprintf(stderr, "myinit: Invalid PID: %s\n", pid_str);
        close(client_fd);
        return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "myinit: Converted PID to %d\n", client_pid);
#endif

    // Проверяем, существует ли процесс
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", client_pid);
    if (access(proc_path, F_OK) != 0) {
        fprintf(stderr, "myinit: Process with PID %d does not exist: %s\n", client_pid, strerror(errno));
        close(client_fd);
        return NULL;
    }

    // Открываем /proc/PID/fd/0, /proc/PID/fd/1, /proc/PID/fd/2
    char fd_path[64];
    int stdin_fd, stdout_fd, stderr_fd;

    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd/0", client_pid);
    stdin_fd = open(fd_path, O_RDWR);
    if (stdin_fd < 0) {
        fprintf(stderr, "myinit: Failed to open %s: %s\n", fd_path, strerror(errno));
        close(client_fd);
        return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "myinit: Opened %s as fd %d\n", fd_path, stdin_fd);
#endif

    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd/1", client_pid);
    stdout_fd = open(fd_path, O_WRONLY);
    if (stdout_fd < 0) {
        fprintf(stderr, "myinit: Failed to open %s: %s\n", fd_path, strerror(errno));
        close(stdin_fd);
        close(client_fd);
        return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "myinit: Opened %s as fd %d\n", fd_path, stdout_fd);
#endif

    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd/2", client_pid);
    stderr_fd = open(fd_path, O_WRONLY);
    if (stderr_fd < 0) {
        fprintf(stderr, "myinit: Failed to open %s: %s\n", fd_path, strerror(errno));
        close(stdin_fd);
        close(stdout_fd);
        close(client_fd);
        return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "myinit: Opened %s as fd %d\n", fd_path, stderr_fd);
#endif

    // Перенаправляем stdin, stdout, stderr
    if (dup2(stdin_fd, STDIN_FILENO) < 0) {
        fprintf(stderr, "myinit: Failed to dup2 stdin: %s\n", strerror(errno));
        close(stdin_fd);
        close(stdout_fd);
        close(stderr_fd);
        close(client_fd);
        return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "myinit: Redirected stdin to fd %d\n", stdin_fd);
#endif

    if (dup2(stdout_fd, STDOUT_FILENO) < 0) {
        fprintf(stderr, "myinit: Failed to dup2 stdout: %s\n", strerror(errno));
        close(stdin_fd);
        close(stdout_fd);
        close(stderr_fd);
        close(client_fd);
        return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "myinit: Redirected stdout to fd %d\n", stdout_fd);
#endif

    if (dup2(stderr_fd, STDERR_FILENO) < 0) {
        fprintf(stderr, "myinit: Failed to dup2 stderr: %s\n", strerror(errno));
        close(stdin_fd);
        close(stdout_fd);
        close(stderr_fd);
        close(client_fd);
        return NULL;
    }
#ifdef DEBUG
    fprintf(stderr, "myinit: Redirected stderr to fd %d\n", stderr_fd);
#endif

    // Закрываем файловые дескрипторы
    close(stdin_fd);
    close(stdout_fd);
    close(stderr_fd);

    // Проверяем доступность /system/bin/sh
    if (access("/system/bin/sh", X_OK) != 0) {
        fprintf(stderr, "myinit: /system/bin/sh is not executable: %s\n", strerror(errno));
    }
#ifdef DEBUG
    else {
        fprintf(stderr, "myinit: /system/bin/sh is available and executable\n");
    }
#endif

    // Выполняем команду через fork и execle
#ifdef DEBUG
    fprintf(stderr, "myinit: Executing command: %s\n", command);
#endif
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "myinit: fork() failed: %s\n", strerror(errno));
    } else if (pid == 0) {
        // Дочерний процесс: запускаем команду через execle
        char *const envp[] = {"HOSTNAME=localhost", NULL};
        execle("/system/bin/sh", "sh", "-c", command, NULL, envp);
        // Если execle возвращает, произошла ошибка
        fprintf(stderr, "myinit: execle() failed to invoke /system/bin/sh: %s\n", strerror(errno));
        _exit(127); // Код 127, как в system() при ошибке вызова оболочки
    } else {
        // Родительский процесс: ждём завершения команды
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            fprintf(stderr, "myinit: waitpid() failed: %s\n", strerror(errno));
        }
#ifdef DEBUG
        else if (WIFEXITED(status)) {
            fprintf(stderr, "myinit: Command completed with exit status %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "myinit: Command terminated by signal %d\n", WTERMSIG(status));
        }
#endif
    }

    // Отправляем уведомление о завершении
    const char *done_msg = "DONE";
    if (write(client_fd, done_msg, strlen(done_msg)) < 0) {
        fprintf(stderr, "myinit: Failed to send DONE to client: %s\n", strerror(errno));
    }
#ifdef DEBUG
    else {
        fprintf(stderr, "myinit: Sent DONE to client\n");
    }
#endif

    // Закрываем клиентский сокет
    close(client_fd);
#ifdef DEBUG
    puts("myinit: Client connection closed");
#endif
    return NULL;
}

// Функция для прослушивания команд через сокет
void command_listener(void) {
    const char *socket_path = "/dev/myinit_socket";
    int server_fd;
    struct sockaddr_un addr;

    // Создаём UNIX-сокет
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "myinit: Failed to create socket: %s\n", strerror(errno));
        exit(1);
    }
    fprintf(stderr, "myinit: Created socket fd %d\n", server_fd);

    // Настраиваем адрес сокета
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Удаляем существующий сокет, если он есть
    unlink(socket_path);

    // Привязываем сокет
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "myinit: Failed to bind socket %s: %s\n", socket_path, strerror(errno));
        close(server_fd);
        exit(1);
    }
    fprintf(stderr, "myinit: Bound socket to %s\n", socket_path);

    // Устанавливаем права доступа
    if (chmod(socket_path, 0666) < 0) {
        fprintf(stderr, "myinit: Failed to set permissions on %s: %s\n", socket_path, strerror(errno));
        unlink(socket_path);
        close(server_fd);
        exit(1);
    }
    fprintf(stderr, "myinit: Set permissions 0666 on %s\n", socket_path);

    // Слушаем подключения
    if (listen(server_fd, 10) < 0) {
        fprintf(stderr, "myinit: Failed to listen on socket: %s\n", strerror(errno));
        unlink(socket_path);
        close(server_fd);
        exit(1);
    }
    fprintf(stderr, "myinit: Listening on socket\n");

    // Принимаем подключения
    while (1) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            fprintf(stderr, "myinit: Failed to allocate memory for client_fd\n");
            continue;
        }

        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            fprintf(stderr, "myinit: Failed to accept connection: %s\n", strerror(errno));
            free(client_fd);
            continue;
        }
#ifdef DEBUG
        fprintf(stderr, "myinit: Accepted client connection, fd %d\n", *client_fd);
#endif

        // Создаём поток для обработки клиента
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, client_fd) != 0) {
            fprintf(stderr, "myinit: Failed to create client thread: %s\n", strerror(errno));
            close(*client_fd);
            free(client_fd);
            continue;
        }

        // Отсоединяем поток
        pthread_detach(client_thread);
    }

    // Закрываем серверный сокет (недостижимо)
    close(server_fd);
    unlink(socket_path);
}

// Функция для запуска слушателя команд
void start_command_listener() {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "myinit: Failed to fork for command listener: %s\n", strerror(errno));
        return;
    } else if (pid == 0) {
        // Дочерний процесс: запускаем слушатель команд
        fprintf(stderr, "myinit: Started command listener in child process with PID %d\n", getpid());
        command_listener();
        exit(0); // Не достигается, так как command_listener работает бесконечно
    } else {
        // Родительский процесс: продолжаем выполнение
        fprintf(stderr, "myinit: Forked command listener with PID %d\n", pid);
    }
}

int main(int argc, char *argv[]) {
    // Проверяем, что программа запущена с правами root
    if (geteuid() != 0) {
        fprintf(stderr, "myinit: This program must be run as root\n");
        return 1;
    }
    fprintf(stderr, "myinit: Running as root\n");

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

        while (1) {
            sleep(1);

            if (kill(1, SIGSTOP) == -1) {
                perror("myinit: kill SIGSTOP");
                continue;
            }

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

            char line[256];
            int data_mounted = 0;
            while (fgets(line, sizeof(line), proc_mounts)) {
                line[strcspn(line, "\n")] = '\0';
                if (strstr(line, " /data ")) {
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

            if (data_mounted)
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

        // Запускаем слушатель команд в дочернем процессе
        start_command_listener();

        puts("myinit: Starting /data/local/tmp/myscript.sh");
        int script_ret = system("sh /data/local/tmp/myscript.sh");
        if (script_ret == -1) {
            fprintf(stderr, "myinit: Failed to execute /data/local/tmp/myscript.sh: %s\n", strerror(errno));
        }
        else {
            fprintf(stderr, "myinit: /data/local/tmp/myscript.sh completed with exit status %d\n", WEXITSTATUS(script_ret));
        }

        puts("myinit: Starting interactive shell on console");
        // Запускаем /system/bin/sh с новым окружением
        char *const envp[] = {"HOSTNAME=localhost", NULL};
        if (execle("/system/bin/sh", "sh", NULL, envp) < 0) {
            fprintf(stderr, "myinit: Failed to exec /system/bin/sh: %s\n", strerror(errno));
            return 1;
        }

        return 0; // Не достигается, так как execl заменяет процесс
    } else {
        // Неверный параметр командной строки
        fprintf(stderr, "Invalid argument. Use '1' as the argument.\n");
        return 1;
    }

    return 0; // Не достигается, так как execl заменяет процесс
}
