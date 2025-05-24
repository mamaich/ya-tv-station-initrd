#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <command>\n", argv[0]);
        return 1;
    }

    const char *socket_path = "/dev/myinit_socket";
    int sock_fd;
    struct sockaddr_un addr;

    // Получаем PID текущего процесса
    pid_t pid = getpid();

//    puts("socket(AF_UNIX, SOCK_STREAM, 0)");
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

//    puts("connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr))");
    // Подключаемся к сокету
    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to connect to %s: %s\n", socket_path, strerror(errno));
        close(sock_fd);
        return 1;
    }

    // Формируем сообщение в формате PID<SOH>command
    char message[1024];
    snprintf(message, sizeof(message), "%d\001%s", pid, argv[1]);

//    puts("write(sock_fd, message, strlen(message))");
    // Отправляем сообщение
    ssize_t bytes_written = write(sock_fd, message, strlen(message));
    if (bytes_written < 0) {
        fprintf(stderr, "Failed to send command: %s\n", strerror(errno));
        close(sock_fd);
        return 1;
    }

//    puts("read(sock_fd, buffer, sizeof(buffer) - 1)");
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