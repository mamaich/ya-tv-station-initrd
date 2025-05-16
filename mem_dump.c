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

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        return 1;
    }

    // Получаем PID из аргумента
    pid_t pid = atoi(argv[1]);
    if (pid <= 0) {
        fprintf(stderr, "Invalid PID: %s\n", argv[1]);
        return 1;
    }

    // Формируем пути к файлам /proc/[pid]/maps и /proc/[pid]/mem
    char maps_path[32], mem_path[32];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

    // Формируем имя выходного файла
    char output_path[64];
    snprintf(output_path, sizeof(output_path), "memory_dump_%d.bin", pid);

    // Открываем /proc/[pid]/maps
    FILE *maps_file = fopen(maps_path, "r");
    if (!maps_file) {
        perror("fopen /proc/[pid]/maps");
        return 1;
    }

    // Открываем /proc/[pid]/mem
    int mem_fd = open(mem_path, O_RDONLY);
    if (mem_fd == -1) {
        perror("open /proc/[pid]/mem");
        fclose(maps_file);
        return 1;
    }

    // Открываем выходной файл
    int out_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1) {
        perror("open output file");
        fclose(maps_file);
        close(mem_fd);
        return 1;
    }

    // Приостанавливаем процесс
    if (kill(pid, SIGSTOP) == -1) {
        perror("kill SIGSTOP");
        fclose(maps_file);
        close(mem_fd);
        close(out_fd);
        return 1;
    }

    // Буфер для чтения maps
    char line[256];
    size_t total_bytes_written = 0;

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

        // Записываем данные в файл
        ssize_t bytes_written = write(out_fd, buffer, bytes_read);
        if (bytes_written == -1) {
            perror("write to output file");
            free(buffer);
            continue;
        }

        total_bytes_written += bytes_written;
        printf("Dumped %zd bytes from region %lx-%lx\n", bytes_written, start_addr, end_addr);

        free(buffer);
    }

    printf("Total bytes written to %s: %zu\n", output_path, total_bytes_written);

    // Возобновляем процесс
    if (kill(pid, SIGCONT) == -1) {
        perror("kill SIGCONT");
    }

    // Закрываем файлы
    fclose(maps_file);
    close(mem_fd);
    close(out_fd);
    return 0;
}