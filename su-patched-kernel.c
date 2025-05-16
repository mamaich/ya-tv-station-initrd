#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>

void print_help(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -c, --command=COMMAND  Execute the specified command using system()\n");
    printf("  -h, --help             Display this help message\n");
}

int main(int argc, char *argv[]) {
    int opt;
    static struct option long_options[] = {
        {"command", required_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    char result[123] = {'e'};
    int enforce_fd = open("/sys/fs/selinux/enforce", O_RDONLY);
    read(enforce_fd, result, 123);
    close(enforce_fd);

    if (setuid(0) == -1 || setgid(0) == -1) {
        perror("setuid/setgid failed");
        return 1;
    }

    while ((opt = getopt_long(argc, argv, "c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                if (optarg) {
                    int result = system(optarg);
                    if (result == -1) {
                        perror("system call failed");
                        return 1;
                    }
                    return result;
                } else {
                    fprintf(stderr, "Error: --command requires an argument\n");
                    return 1;
                }
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                /* Ignore unknown options */
                break;
        }
    }
    
    return system("/system/bin/sh");
}