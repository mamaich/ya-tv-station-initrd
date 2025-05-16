#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

// Функция для чтения памяти процесса
int read_memory(int mem_fd, off_t addr, char *buffer, size_t len) {
    if (lseek(mem_fd, addr, SEEK_SET) == -1) {
        perror("lseek");
        return -1;
    }
    ssize_t bytes_read = read(mem_fd, buffer, len);
    if (bytes_read == -1) {
        perror("read");
        return -1;
    }
    return bytes_read;
}

// Функция для записи в память процесса
int write_memory(int mem_fd, off_t addr, const char *buffer, size_t len) {
    if (lseek(mem_fd, addr, SEEK_SET) == -1) {
        perror("lseek");
        return -1;
    }
    if (write(mem_fd, buffer, len) == -1) {
        perror("write");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <pid> <search_string> <replace_string>\n", argv[0]);
        return 1;
    }

    // Получаем PID и строки из аргументов
    pid_t pid = atoi(argv[1]);
    const char *search_string = argv[2];
    char *replace_string = argv[3];

    if (pid <= 0) {
        fprintf(stderr, "Invalid PID: %s\n", argv[1]);
        return 1;
    }

    if (strlen(search_string) == 0) {
        fprintf(stderr, "Search string cannot be empty\n");
        return 1;
    }

    // Формируем пути к файлам /proc/[pid]/maps и /proc/[pid]/mem
    char maps_path[32], mem_path[32];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

    // Открываем /proc/[pid]/maps
    FILE *maps_file = fopen(maps_path, "r");
    if (!maps_file) {
        perror("fopen /proc/[pid]/maps");
        return 1;
    }

    // Открываем /proc/[pid]/mem
    int mem_fd = open(mem_path, O_RDWR);
    if (mem_fd == -1) {
        perror("open /proc/[pid]/mem");
        fclose(maps_file);
        return 1;
    }

    // Приостанавливаем процесс
    if (kill(pid, SIGSTOP) == -1) {
        perror("kill SIGSTOP");
        fclose(maps_file);
        close(mem_fd);
        return 1;
    }

    // Буфер для чтения maps
    char line[256];
    size_t search_len = strlen(search_string);
    size_t replace_len = strlen(replace_string);
    int found = 0;

    if(search_len>replace_len)
    {
        fprintf(stderr, "Warning: %d (search_len) > %d (replace_len), padding replace string with zeroes.\n",search_len,replace_len);
        replace_string = malloc(search_len+1);
        memset(replace_string,0,search_len+1);
        strcpy(replace_string,search_string);
        replace_len=search_len;
    }

    // Читаем /proc/[pid]/maps построчно
    while (fgets(line, sizeof(line), maps_file)) {
        unsigned long start_addr, end_addr;
        char perms[5];

        // Парсим строку maps (формат: start-end perms ...)
        if (sscanf(line, "%lx-%lx %s", &start_addr, &end_addr, perms) != 3) {
            continue;
        }

        // Проверяем, доступна ли область для чтения
        if (strchr(perms, 'r') == NULL) {
            continue; // Пропускаем области без права чтения
        }

        // Читаем память области
        size_t region_size = end_addr - start_addr;
        char *buffer = malloc(region_size);
        if (!buffer) {
            fprintf(stderr, "Failed to allocate buffer for region %lx-%lx\n", start_addr, end_addr);
            continue;
        }

        int bytes_read = read_memory(mem_fd, start_addr, buffer, region_size);
        if (bytes_read <= 0) {
            free(buffer);
            continue;
        }

        // Ищем строку search_string в буфере
        for (size_t i = 0; i <= bytes_read - search_len; i++) {
            if (memcmp(buffer + i, search_string, search_len) == 0) {
                printf("Found '%s' at address %lx\n", search_string, start_addr + i);
                found = 1;

                // Заменяем строку на replace_string
                if (write_memory(mem_fd, start_addr + i, replace_string, replace_len) == -1) {
                    fprintf(stderr, "Failed to replace string at %lx\n", start_addr + i);
                } else {
                    printf("Replaced with '%s' at address %lx\n", replace_string, start_addr + i);
                }
            }
        }

        free(buffer);
    }

    if (!found) {
        printf("String '%s' not found in process memory\n", search_string);
    }

    // Возобновляем процесс
    if (kill(pid, SIGCONT) == -1) {
        perror("kill SIGCONT");
    }

    // Закрываем файлы
    fclose(maps_file);
    close(mem_fd);
    return 0;
}