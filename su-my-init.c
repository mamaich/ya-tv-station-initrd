#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <getopt.h>

int call_myinit(char* cmd) {
    const char *socket_path = "/dev/myinit_socket";
    int sock_fd;
    struct sockaddr_un addr;

    // Получаем PID текущего процесса
    pid_t pid = getpid();

    // Получаем текущий рабочий каталог
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "Failed to get current working directory: %s\n", strerror(errno));
        return 1;
    }

    // Создаём UNIX-сокет
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return 1;
    }

    // Настраиваем адрес сокета
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Подключаемся к сокету
    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to connect to %s: %s\n", socket_path, strerror(errno));
        close(sock_fd);
        return 1;
    }

    // Формируем сообщение в формате PID<SOH>command<SOH>cwd
    char message[2048];
    snprintf(message, sizeof(message), "%d\001%s\001%s", pid, cmd, cwd);

    // Отправляем сообщение
    ssize_t bytes_written = write(sock_fd, message, strlen(message));
    if (bytes_written < 0) {
        fprintf(stderr, "Failed to send command: %s\n", strerror(errno));
        close(sock_fd);
        return 1;
    }

    // Ждём уведомления о завершении команды
    char buffer[16];
    ssize_t bytes_read = read(sock_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        fprintf(stderr, "Failed to read completion signal: %s\n", strerror(errno));
        close(sock_fd);
        return 1;
    }
    buffer[bytes_read] = '\0';

    if (strcmp(buffer, "DONE") != 0) {
        fprintf(stderr, "Unexpected response from server: %s\n", buffer);
        close(sock_fd);
        return 1;
    }

    // Закрываем сокет
    close(sock_fd);
    return 0;
}

void print_help(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -c, --command=COMMAND  Execute the specified command\n");
    printf("  -h, --help             Display this help message\n");
}

int main(int argc, char *argv[]) {
    int opt;
    static struct option long_options[] = {
        {"command", required_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                if (optarg) {
                    return call_myinit(optarg);
                } else {
                    fprintf(stderr, "Error: --command requires an argument\n");
                    return 1;
                }
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                print_help(argv[0]);
                return 1;
        }
    }

    return call_myinit("/system/bin/sh");
}