#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <cctype>
#include <cerrno>

// Функция для проверки, является ли строка числом (PID)
bool is_number(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

// Функция для обработки нового процесса
void on_new_process(int pid) {
    std::string proc_path = "/proc/" + std::to_string(pid) + "/cmdline";
    std::string cmdline;
    char buffer[256] = {0};

    int fd = open(proc_path.c_str(), O_RDONLY);
    if (fd >= 0) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            // Заменяем '\0' на пробелы для читаемости
            for (char* p = buffer; *p; ++p) {
                if (*p == '\0') *p = ' ';
            }
            cmdline = buffer;
        } else {
            cmdline = "";
        }
        close(fd);
    } else {
        cmdline = "";
    }

    std::cout << "New process: PID=" << pid << ", cmdline=" << cmdline << std::endl;
    system(("./on_new.sh "+std::to_string(pid)+" \""+cmdline+"\"").c_str());
}

// Функция для обработки завершившегося процесса
void on_terminated_process(int pid) {
    std::cout << "Terminated process: PID=" << pid << std::endl;
}

int main() {
    std::vector<int> current_pids;

    std::cout << "Monitoring /proc for process changes..." << std::endl;

    while (true) {
        DIR* dir = opendir("/proc");
        if (!dir) {
            std::cerr << "opendir /proc: " << strerror(errno) << std::endl;
            return 1;
        }

        // Собираем новые PID
        std::vector<int> new_pids;
        struct dirent* entry;
        while ((entry = readdir(dir))) {
            if (is_number(entry->d_name)) {
                int pid = std::stoi(entry->d_name);
                new_pids.push_back(pid);
            }
        }
        closedir(dir);

        // Проверяем новые процессы
        for (int pid : new_pids) {
            if (std::find(current_pids.begin(), current_pids.end(), pid) == current_pids.end()) {
                on_new_process(pid);
            }
        }

        // Проверяем завершившиеся процессы
        std::vector<int> terminated_pids;
        for (int pid : current_pids) {
            if (std::find(new_pids.begin(), new_pids.end(), pid) == new_pids.end()) {
                terminated_pids.push_back(pid);
            }
        }
        for (int pid : terminated_pids) {
            on_terminated_process(pid);
            current_pids.erase(std::remove(current_pids.begin(), current_pids.end(), pid), current_pids.end());
        }

        // Обновляем текущий список PID
        current_pids = new_pids;

        // Задержка 1 секунда
        sleep(5);
    }

    return 0;
}
