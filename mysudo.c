#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <signal.h>

#define VERSION "1.0"

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
    printf("Usage: %s [options] [--] [command [arg ...]]\n", prog_name);
    printf("Options:\n");
    printf("  -s, --shell            Run the shell specified by SHELL env or /system/bin/sh\n");
    printf("  -i, --login            Same as --shell\n");
    printf("  -n, --non-interactive  Redirect stdin to /dev/null\n");
    printf("  -V, --version          Display version information\n");
    printf("  -h, --help             Display this help message\n");
    printf("  --                     Treat all following arguments as command and its args\n");
}

int main(int argc, char *argv[]) {
    // Игнорируем сигналы, как в myinit.c
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    // Если нет аргументов, выводим справку
    if (argc == 1) {
        print_help(argv[0]);
        return 0;
    }

    int opt;
    int shell_mode = 0;
    int non_interactive = 0;
    static struct option long_options[] = {
        {"shell", no_argument, 0, 's'},
        {"login", no_argument, 0, 'i'},
        {"non-interactive", no_argument, 0, 'n'},
        {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // Parse options, stop at first non-option argument
    optind = 1; // Reset optind for clarity
    while ((opt = getopt_long(argc, argv, "+sinVh", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
            case 'i':
                shell_mode = 1;
                break;
            case 'n':
                non_interactive = 1;
                break;
            case 'V':
                printf("%s version %s\n", argv[0], VERSION);
                return 0;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                // Stop parsing at unrecognized option
                break;
        }
    }

    // Handle non-interactive mode
    if (non_interactive) {
        int dev_null = open("/dev/null", O_RDONLY);
        if (dev_null < 0) {
            fprintf(stderr, "Failed to open /dev/null: %s\n", strerror(errno));
            return 1;
        }
        if (dup2(dev_null, STDIN_FILENO) < 0) {
            fprintf(stderr, "Failed to redirect stdin to /dev/null: %s\n", strerror(errno));
            close(dev_null);
            return 1;
        }
        close(dev_null);
    }

    // Determine the command to execute
    char command[2048];
    if (shell_mode) {
        // Use SHELL environment variable or default to /system/bin/sh
        char *cmd = getenv("SHELL");
        if (!cmd || *cmd == '\0') {
            cmd = "/system/bin/sh";
        }
        snprintf(command, sizeof(command), "%s", cmd);
    } else if (optind < argc) {
        // Construct command with all remaining arguments
        char *cmd_start = argv[optind];
        size_t len = strlen(cmd_start);
        strncpy(command, cmd_start, sizeof(command) - 1);
        command[sizeof(command) - 1] = '\0';
        size_t pos = len;

        // Append all remaining arguments, including after --
        for (int i = optind + 1; i < argc; i++) {
            size_t arg_len = strlen(argv[i]);
            if (pos + arg_len + 2 >= sizeof(command)) {
                fprintf(stderr, "Command too long\n");
                return 1;
            }
            command[pos] = ' ';
            pos++;
            strncpy(command + pos, argv[i], sizeof(command) - pos - 1);
            pos += arg_len;
            command[pos] = '\0';
        }
    } else {
        // No command specified and not in shell mode, show help
        print_help(argv[0]);
        return 1;
    }

    return call_myinit(command);
}