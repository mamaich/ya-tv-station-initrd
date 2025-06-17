#define _GNU_SOURCE
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
#include <sys/ptrace.h>

#define TARGET_PROGRAM "/init"
#define LOG_PREFIX "myinit: "

#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...) do { \
    fprintf(stdout, LOG_PREFIX fmt, ##__VA_ARGS__); \
    fflush(stdout); \
} while (0)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

// Функция для обработки клиентского подключения
void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    DEBUG_PRINT("Received new client connection\n");

    // Читаем команду из сокета (формат: PID<SOH>command<SOH>cwd)
    char buffer[1024];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        fprintf(stderr, LOG_PREFIX "Failed to read from client socket: %s\n", strerror(errno));
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    DEBUG_PRINT("Received message (%zd bytes): '%s'\n", bytes_read, buffer);

    // Парсим PID, команду и текущий каталог, используя SOH (\001) как разделитель
    char *pid_str = strtok(buffer, "\001");
    char *command = strtok(NULL, "\001");
    char *cwd = strtok(NULL, "");
    if (!pid_str || !command || !cwd) {
        fprintf(stderr, LOG_PREFIX "Invalid message format, expected PID<SOH>command<SOH>cwd, got pid_str='%s', command='%s', cwd='%s'\n",
                pid_str ? pid_str : "(null)", command ? command : "(null)", cwd ? cwd : "(null)");
        close(client_fd);
        return NULL;
    }
    DEBUG_PRINT("Parsed PID='%s', command='%s', cwd='%s'\n", pid_str, command, cwd);

    // Преобразуем PID в число
    pid_t client_pid = atoi(pid_str);
    if (client_pid <= 0) {
        fprintf(stderr, LOG_PREFIX "Invalid PID: %s\n", pid_str);
        close(client_fd);
        return NULL;
    }
    DEBUG_PRINT("Converted PID to %d\n", client_pid);

    // Проверяем, существует ли процесс
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", client_pid);
    if (access(proc_path, F_OK) != 0) {
        fprintf(stderr, LOG_PREFIX "Process with PID %d does not exist: %s\n", client_pid, strerror(errno));
        close(client_fd);
        return NULL;
    }

    // Открываем /proc/PID/fd/0, /proc/PID/fd/1, /proc/PID/fd/2
    char fd_path[64];
    int stdin_fd, stdout_fd, stderr_fd;

    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd/0", client_pid);
    stdin_fd = open(fd_path, O_RDWR);
    if (stdin_fd < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to open %s: %s\n", fd_path, strerror(errno));
        close(client_fd);
        return NULL;
    }
    DEBUG_PRINT("Opened %s as fd %d\n", fd_path, stdin_fd);

    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd/1", client_pid);
    stdout_fd = open(fd_path, O_WRONLY);
    if (stdout_fd < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to open %s: %s\n", fd_path, strerror(errno));
        close(stdin_fd);
        close(client_fd);
        return NULL;
    }
    DEBUG_PRINT("Opened %s as fd %d\n", fd_path, stdout_fd);

    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd/2", client_pid);
    stderr_fd = open(fd_path, O_WRONLY);
    if (stderr_fd < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to open %s: %s\n", fd_path, strerror(errno));
        close(stdin_fd);
        close(stdout_fd);
        close(client_fd);
        return NULL;
    }
    DEBUG_PRINT("Opened %s as fd %d\n", fd_path, stderr_fd);

    // Устанавливаем текущий каталог
    if (chdir(cwd) < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to change directory to %s: %s\n", cwd, strerror(errno));
        close(stdin_fd);
        close(stdout_fd);
        close(stderr_fd);
        close(client_fd);
        return NULL;
    }
    DEBUG_PRINT("Changed current directory to %s\n", cwd);

    // Выполняем команду через fork и execle
    DEBUG_PRINT("Executing command: %s\n", command);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, LOG_PREFIX "fork() failed: %s\n", strerror(errno));
        close(stdin_fd);
        close(stdout_fd);
        close(stderr_fd);
        close(client_fd);
        return NULL;
    } else if (pid == 0) {
        signal(SIGTERM, SIG_IGN);
        signal(SIGINT, SIG_IGN);
        signal(SIGPIPE, SIG_IGN);
        signal(SIGHUP, SIG_IGN);
        // Дочерний процесс: перенаправляем потоки и запускаем команду
        if (dup2(stdin_fd, STDIN_FILENO) < 0) {
            fprintf(stderr, LOG_PREFIX "Failed to dup2 stdin: %s\n", strerror(errno));
            close(stdin_fd);
            close(stdout_fd);
            close(stderr_fd);
            close(client_fd);
            _exit(1);
        }
        DEBUG_PRINT("Redirected stdin to fd %d\n", stdin_fd);

        if (dup2(stdout_fd, STDOUT_FILENO) < 0) {
            fprintf(stderr, LOG_PREFIX "Failed to dup2 stdout: %s\n", strerror(errno));
            close(stdin_fd);
            close(stdout_fd);
            close(stderr_fd);
            close(client_fd);
            _exit(1);
        }
        DEBUG_PRINT("Redirected stdout to fd %d\n", stdout_fd);

        if (dup2(stderr_fd, STDERR_FILENO) < 0) {
            fprintf(stderr, LOG_PREFIX "Failed to dup2 stderr: %s\n", strerror(errno));
            close(stdin_fd);
            close(stdout_fd);
            close(stderr_fd);
            close(client_fd);
            _exit(1);
        }
        DEBUG_PRINT("Redirected stderr to fd %d\n", stderr_fd);

        // Закрываем файловые дескрипторы
        close(stdin_fd);
        close(stdout_fd);
        close(stderr_fd);
        DEBUG_PRINT("Closed file descriptors for client PID %d\n", client_pid);

        // Запускаем команду через execle
        char *const envp[] = {"HOSTNAME=localhost", "TERM=dumb", NULL};
        execle("/system/bin/sh", "sh", "-c", command, NULL, envp);
        // Если execle возвращает, произошла ошибка
        fprintf(stderr, LOG_PREFIX "execle() failed to invoke /system/bin/sh: %s\n", strerror(errno));
        _exit(127);
    } else {
        // Родительский процесс: закрываем дескрипторы
        close(stdin_fd);
        close(stdout_fd);
        close(stderr_fd);
        DEBUG_PRINT("Closed file descriptors for client PID %d in parent\n", client_pid);

        // Ждём завершения команды
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            fprintf(stderr, LOG_PREFIX "waitpid() failed: %s\n", strerror(errno));
        } else if (WIFEXITED(status)) {
            DEBUG_PRINT("Command completed with exit status %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            DEBUG_PRINT("Command terminated by signal %d\n", WTERMSIG(status));
        }

        // Отправляем уведомление о завершении
        const char *done_msg = "DONE";
        if (write(client_fd, done_msg, strlen(done_msg)) < 0) {
            fprintf(stderr, LOG_PREFIX "Failed to send DONE to client: %s\n", strerror(errno));
        } else {
            DEBUG_PRINT("Sent DONE to client\n");
        }

        // Закрываем клиентский сокет
        close(client_fd);
        DEBUG_PRINT("Client connection closed\n");
    }

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
        fprintf(stderr, LOG_PREFIX "Failed to create socket: %s\n", strerror(errno));
        exit(1);
    }
    DEBUG_PRINT("Created socket fd %d\n", server_fd);

    // Настраиваем адрес сокета
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Удаляем существующий сокет, если он есть
    unlink(socket_path);
    DEBUG_PRINT("Removed existing socket at %s\n", socket_path);

    // Привязываем сокет
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to bind socket %s: %s\n", socket_path, strerror(errno));
        close(server_fd);
        exit(1);
    }
    DEBUG_PRINT("Bound socket to %s\n", socket_path);

    // Устанавливаем права доступа
    if (chmod(socket_path, 0666) < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to set permissions on %s: %s\n", socket_path, strerror(errno));
        unlink(socket_path);
        close(server_fd);
        exit(1);
    }
    DEBUG_PRINT("Set permissions 0666 on %s\n", socket_path);

    // Слушаем подключения
    if (listen(server_fd, 10) < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to listen on socket: %s\n", strerror(errno));
        unlink(socket_path);
        close(server_fd);
        exit(1);
    }
    DEBUG_PRINT("Listening on socket\n");

    // Принимаем подключения
    while (1) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            fprintf(stderr, LOG_PREFIX "Failed to allocate memory for client_fd\n");
            continue;
        }

        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            fprintf(stderr, LOG_PREFIX "Failed to accept connection: %s\n", strerror(errno));
            free(client_fd);
            continue;
        }
        DEBUG_PRINT("Accepted client connection, fd %d\n", *client_fd);

        // Создаём поток для обработки клиента
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, client_fd) != 0) {
            fprintf(stderr, LOG_PREFIX "Failed to create client thread: %s\n", strerror(errno));
            close(*client_fd);
            free(client_fd);
            continue;
        }

        // Отсоединяем поток
        pthread_detach(client_thread);
        DEBUG_PRINT("Detached client thread for fd %d\n", *client_fd);
    }

    // Закрываем серверный сокет (недостижимо)
    close(server_fd);
    unlink(socket_path);
    DEBUG_PRINT("Closed server socket and unlinked %s\n", socket_path);
}

// Функция для запуска слушателя команд
void start_command_listener() {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to fork for command listener: %s\n", strerror(errno));
        return;
    } else if (pid == 0) {
        // Удалены избыточные signal(SIGTERM, SIG_IGN); signal(SIGINT, SIG_IGN); signal(SIGPIPE, SIG_IGN);
        // Наследуются из main
        // Дочерний процесс: запускаем слушатель команд
        DEBUG_PRINT("Started command listener in child process with PID %d\n", getpid());
        command_listener();
        exit(0); // Не достигается, так как command_listener работает бесконечно
    } else {
        // Родительский процесс: продолжаем выполнение
        DEBUG_PRINT("Forked command listener with PID %d\n", pid);
    }
}

// Функция для создания и монтирования proc
int mount_proc(const char *mountpoint, const char *context) {
    struct stat st;
    if (stat(mountpoint, &st) < 0) {
        if (mkdir(mountpoint, 0555) < 0) {
            fprintf(stderr, LOG_PREFIX "Failed to create %s directory (%s): %s\n", mountpoint, context, strerror(errno));
            return 1;
        }
        DEBUG_PRINT("Created directory %s\n", mountpoint);
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, LOG_PREFIX "%s exists but is not a directory (%s)\n", mountpoint, context);
        return 1;
    }

    if (mount("proc", mountpoint, "proc", 0, NULL) < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to mount %s (%s): %s\n", mountpoint, context, strerror(errno));
        return 1;
    }
    DEBUG_PRINT("Mounted proc at %s\n", mountpoint);

    return 0;
}

int attach_namespace() {
    if (mount_proc("/proc", "before getting handle to /proc/1/ns")) {
        DEBUG_PRINT("Failed to mount /proc\n");
        return 1;
    }

    int ns_fd = open("/proc/1/ns/mnt", O_RDONLY);
    if (ns_fd < 0) {
        DEBUG_PRINT("Failed to open /proc/1/ns/mnt: %s\n", strerror(errno));
        umount2("/proc", 0);
        return 1;
    }
    DEBUG_PRINT("Opened /proc/1/ns/mnt as fd %d\n", ns_fd);

    // Размонтируем /proc до setns
    if (umount2("/proc", 0) < 0) {
        DEBUG_PRINT("Failed to unmount /proc (before setns): %s\n", strerror(errno));
        close(ns_fd);
        return 1;
    }
    DEBUG_PRINT("Unmounted /proc before setns\n");

    // Переключаемся в пространство имен
    if (setns(ns_fd, CLONE_NEWNS) == -1) {
        perror(LOG_PREFIX "setns");
        close(ns_fd);
        return 1;
    }
    DEBUG_PRINT("Successfully switched to /proc/1/ns/mnt namespace\n");

    // Закрываем дескриптор пространства имен
    close(ns_fd);
    DEBUG_PRINT("Closed namespace fd\n");

    // Проверяем пространство имен (опционально)
    char buf[256];
    ssize_t len = readlink("/proc/self/ns/mnt", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        DEBUG_PRINT("Current mount namespace: %s\n", buf);
    } else {
        perror(LOG_PREFIX "readlink");
    }

    return 0;
}

// Функция проверки монтирования точки
int check_mount(const char *mounts_file, const char *mount_point) {
    FILE *proc_mounts = fopen(mounts_file, "r");
    if (!proc_mounts) {
        fprintf(stderr, LOG_PREFIX "Failed to open %s: %s\n", mounts_file, strerror(errno));
        return 0;
    }

    char line[256];
    int mounted = 0;
    while (fgets(line, sizeof(line), proc_mounts)) {
        line[strcspn(line, "\n")] = '\0';
        if (strstr(line, mount_point)) {
            mounted = 1;
            break;
        }
    }
    fclose(proc_mounts);
    return mounted;
}

void handle_exec_event(pid_t parent_pid) {
    // Реагируем на вызов exec
    fprintf(stderr, LOG_PREFIX "Parent process called exec()\n");

    attach_namespace();
    DEBUG_PRINT("Attached to namespace of PID 1\n");

    // Создаем файл /dev/enforce с содержимым "1" (один байт)
    int fd = open("/dev/enforce", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror(LOG_PREFIX "open /dev/enforce failed");
        exit(1);
    }
    if (write(fd, "1", 1) != 1) {
        perror(LOG_PREFIX "write to /dev/enforce failed");
        close(fd);
        exit(1);
    }
    close(fd);
    DEBUG_PRINT("Created and wrote '1' to /dev/enforce\n");

    // Выполняем bind mount /dev/enforce на /sys/fs/selinux/enforce
    if (mount("/dev/enforce", "/sys/fs/selinux/enforce", NULL, MS_BIND, NULL) == -1) {
        perror(LOG_PREFIX "bind mount failed");
        exit(1);
    }
    DEBUG_PRINT("Mounted /dev/enforce to /sys/fs/selinux/enforce\n");

    // Отсоединяемся от родителя
    ptrace(PTRACE_DETACH, parent_pid, NULL, NULL);
    DEBUG_PRINT("Detached from parent PID %d\n", parent_pid);

    start_command_listener();

    // Проверяем монтирование /data
    DEBUG_PRINT("Waiting for /data mount\n");
    while (1) {
        sleep(1);
        if (check_mount("/proc/1/mounts", " /data ")) {
            break;
        }
    }

    DEBUG_PRINT("Starting /data/local/tmp/myscript.sh\n");
    int script_ret = system("sh /data/local/tmp/myscript.sh");
    if (script_ret == -1) {
        fprintf(stderr, LOG_PREFIX "Failed to execute /data/local/tmp/myscript.sh: %s\n", strerror(errno));
    } else {
        fprintf(stderr, LOG_PREFIX "/data/local/tmp/myscript.sh completed with exit status %d\n", WEXITSTATUS(script_ret));
    }

    DEBUG_PRINT("Starting interactive shell on console\n");
    // Запускаем /system/bin/sh с новым окружением
    char *const envp[] = {"HOSTNAME=localhost", NULL};
    if (execle("/system/bin/sh", "sh", NULL, envp) < 0) {
        fprintf(stderr, LOG_PREFIX "Failed to exec /system/bin/sh: %s\n", strerror(errno));
    }

    exit(0);
}

void debug_process() {
    // Счетчик для пропуска первого вызова exec
    int exec_count = 0;
    pid_t parent_pid = getppid();
    DEBUG_PRINT("Starting debug process for parent PID %d\n", parent_pid);

    // Подключаемся к родительскому процессу для трассировки
    if (ptrace(PTRACE_ATTACH, parent_pid, NULL, NULL) == -1) {
        perror(LOG_PREFIX "ptrace attach failed");
        exit(1);
    }
    DEBUG_PRINT("Attached to parent PID %d\n", parent_pid);

    // Ждем, пока родительский процесс не остановится
    wait(NULL);
    DEBUG_PRINT("Parent PID %d stopped\n", parent_pid);

    // Настраиваем трассировку события exec
    if (ptrace(PTRACE_SETOPTIONS, parent_pid, NULL, PTRACE_O_TRACEEXEC) == -1) {
        perror(LOG_PREFIX "ptrace setoptions failed");
        exit(1);
    }
    DEBUG_PRINT("Set ptrace options for exec tracing\n");

    // Продолжаем выполнение родителя
    if (ptrace(PTRACE_CONT, parent_pid, NULL, NULL) == -1) {
        perror(LOG_PREFIX "ptrace cont failed");
        exit(1);
    }
    DEBUG_PRINT("Continued parent PID %d\n", parent_pid);

    // Ожидаем событий от родителя
    int status;
    while (1) {
        if (wait(&status) == -1) {
            perror(LOG_PREFIX "wait failed");
            exit(1);
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            // Родитель завершился
            DEBUG_PRINT("Parent PID %d terminated\n", parent_pid);
            break;
        }

        if (WIFSTOPPED(status)) {
            // Проверяем, вызвана ли остановка событием exec
            if ((status >> 8) == (SIGTRAP | (PTRACE_EVENT_EXEC << 8))) {
                // Обнаружен вызов exec в родительском процессе
                exec_count++;
                DEBUG_PRINT("Detected exec event %d in parent PID %d\n", exec_count, parent_pid);
                if (exec_count > 1) {
                    // Реагируем только на второй и последующие вызовы exec
                    handle_exec_event(parent_pid);
                }
            } else {
                // Остановка вызвана сигналом, пересылаем его родителю
                int sig = WSTOPSIG(status);
                DEBUG_PRINT("Received signal %d for parent PID %d\n", sig, parent_pid);
                if (ptrace(PTRACE_CONT, parent_pid, NULL, sig) == -1) {
                    perror(LOG_PREFIX "ptrace cont failed");
                    exit(1);
                }
                continue;
            }
        }

        // Продолжаем выполнение родителя
        if (ptrace(PTRACE_CONT, parent_pid, NULL, NULL) == -1) {
            perror(LOG_PREFIX "ptrace cont failed");
            exit(1);
        }
        DEBUG_PRINT("Continued parent PID %d after event\n", parent_pid);
    }

    // Отсоединяемся от родителя перед выходом
    ptrace(PTRACE_DETACH, parent_pid, NULL, NULL);
    DEBUG_PRINT("Detached from parent PID %d before exit\n", parent_pid);
    exit(0);
}

int main() {
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN); // Добавлено игнорирование SIGHUP

    pid_t pid = fork();

    if (pid == -1) {
        perror(LOG_PREFIX "fork failed");
        exit(1);
    }
    DEBUG_PRINT("Forked, child PID %d\n", pid);

    if (pid == 0) { // Дочерний процесс
        debug_process();
    } else { // Родительский процесс
        // Даем дочернему процессу время на подключение ptrace
        sleep(1);
        DEBUG_PRINT("Parent waiting for child to attach\n");
        
        // Запускаем указанную программу
        execl(TARGET_PROGRAM, TARGET_PROGRAM, NULL);
        
        // Если execl вернул управление, произошла ошибка
        perror(LOG_PREFIX "execl failed");
        exit(1);
    }

    return 0;
}
