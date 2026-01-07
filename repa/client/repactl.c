#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <termios.h>

#define MAX_STRING_LEN 1024*1024

typedef struct {
    char addr[256];
    int  port;
    char user[64];
    bool user_specified;
} options;

int read_password(char *buf, size_t size){
    struct termios oldt;
    struct termios newt;
    if (tcgetattr(0, &oldt) != 0) {
        perror("tcgetattr");
        return -1;
    }
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    if (tcsetattr(0, TCSAFLUSH, &newt) != 0) {
        perror("tcsetattr");
        return -1;
    }

    fprintf(stdout, "Password: ");
    fflush(stdout);

    if (fgets(buf, (int)size, stdin) == NULL) {
        tcsetattr(0, TCSAFLUSH, &oldt);
        fprintf(stderr, "Error reading password\n");
        return -1;
    }

    tcsetattr(0, TCSAFLUSH, &oldt);
    fprintf(stdout, "\n");

    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') {
        buf[len-1] = '\0';
    }

    return 0;
}

void options_init(options *opt){
    memset(opt, 0, sizeof(*opt));
    strncpy(opt->addr, "127.0.0.1", sizeof(opt->addr));
    opt->port = 6380;
    opt->user_specified = false;
}

static void print_help(void){
    fprintf(stdout,"Usage: [--addr <address>] [--port <port>] [--user <name>]\n");
    fprintf(stdout, "Simple Repa client (repactl minimal)\n");
}

int parse_args(options *opt, int argc, char **argv){
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--addr") == 0) {
            if (i+1 >= argc) {
                fprintf(stderr, "--addr requires value\n");
                return -1;
            }
            strncpy(opt->addr, argv[i+1], sizeof(opt->addr));
            opt->addr[sizeof(opt->addr)-1] = '\0';
            i += 2;
        } else if (strcmp(argv[i], "--port") == 0) {
            if (i+1 >= argc) {
                fprintf(stderr, "--port requires value\n");
                return -1;
            }
            opt->port = atoi(argv[i+1]);
            if (opt->port <= 1024 || opt->port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", argv[i+1]);
                return -1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--user") == 0) {
            if (i+1 >= argc) {
                fprintf(stderr, "--user requires value\n");
                return -1;
            }
            strncpy(opt->user, argv[i+1], sizeof(opt->user));
            opt->user[sizeof(opt->user)-1] = '\0';
            opt->user_specified = true;
            i += 2;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            exit(0);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int connect_to_server(const char *addr, int port){
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;
    int sock = -1;
    int ret;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    ret = getaddrinfo(addr, port_str, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) {
            continue;
        }

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);

    if (sock < 0) {
        fprintf(stderr, "Could not connect to %s:%d\n", addr, port);
        return -1;
    }

    return sock;
}

int write_all(int fd, const void *buf, size_t len){
    const char *p = (const char *)buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        left -= (size_t)n;
        p += n;
    }
    return 0;
}

int send_resp_array(int fd, int argc, char **argv){
    char header[64];
    int n = snprintf(header, sizeof(header), "*%d\r\n", argc);
    if (n < 0) {
        return -1;
    }
    if (write_all(fd, header, (size_t)n) < 0) {
        return -1;
    }

    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        size_t len = strlen(arg);

        n = snprintf(header, sizeof(header), "$%zu\r\n", len);
        if (n < 0) {
            return -1;
        }
        if (write_all(fd, header, (size_t)n) < 0) {
            return -1;
        }

        if (write_all(fd, arg, len) < 0) {
            return -1;
        }
        if (write_all(fd, "\r\n", 2) < 0) {
            return -1;
        }
    }

    return 0;
}

int recv_line(int fd, char *buf, size_t maxlen){
    size_t pos = 0;
    while (pos + 1 < maxlen) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            if (pos == 0) return 0;
            break;
        }
        if (c == '\r') {
            char c2;
            n = read(fd, &c2, 1);
            if (n <= 0) {
                return -1;
            }
            if (c2 != '\n') {
                return -1;
            }
            buf[pos] = '\0';
            return 1;
        } else {
            buf[pos] = c;
            pos++;
        }
    }

    buf[pos] = '\0';
    return 1;
}

int read_and_print_resp(int fd){
    char line[1024];
    int r = recv_line(fd, line, sizeof(line));
    if (r <= 0) {
        if (r == 0) {
            fprintf(stderr, "connection closed by server\n");
        } else {
            perror("read");
        }
        return -1;
    }

    if (line[0] == '\0') {
        printf("empty response\n");
        return 0;
    }

    char type = line[0];
    char *payload = line + 1;

    switch (type) {
        case '+':
            printf("%s\n", payload);
            break;

        case '-':
            printf("(error) %s\n", payload);
            break;

        case ':': 
            printf("(integer) %s\n", payload);
            break;

        case '$': {
            long long len = atoll(payload);
            if (len == -1) {
                printf("(nil)\n");
                return 0;
            }
            if (len < 0 || len > MAX_STRING_LEN) {
                return -1;
            }
            char *buf = (char*)malloc((size_t)len + 1);
            if (buf == NULL) {
                printf("no memory\n");
                return -1;
            }
            size_t got = 0;
            while (got < (size_t)len) {
                ssize_t n = read(fd, buf + got, (size_t)len - got);
                if (n < 0) {
                    if (errno == EINTR){
                        continue;
                    }
                    free(buf);
                    perror("read");
                    return -1;
                }
                if (n == 0) {
                    free(buf);
                    fprintf(stderr, "connection closed\n");
                    return -1;
                }
                got += (size_t)n;
            }
            buf[len] = '\0';
            char crlf[2];
            if (read(fd, crlf, 2) != 2) {
                free(buf);
                fprintf(stderr, "protocol error\n");
                return -1;
            }
            printf("%s\n", buf);
            free(buf);
            break;
        }

        default:
            printf("(unknown RESP type '%c') %s\n", type, payload);
            break;
    }
    return 0;
}

int split_line(char *line, char **argv, int max_args){
    int argc = 0;
    char *p = line;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    while (*p && argc < max_args) {
        argv[argc] = p;

        while (*p != '\0' && !isspace((unsigned char)*p)){
            p++;
        } 
        if (*p == '\0') {
            argc++;
            break;
        }
        *p = '\0';
        p++;
        while (*p && isspace((unsigned char)*p)){
            p++;
        }
        argc++;
    }

    return argc;
}

int main(int argc, char **argv){
    options opt;
    options_init(&opt);

    if (parse_args(&opt, argc, argv) != 0) {
        print_help();
        return 1;
    }

    if (!opt.user_specified) {
        printf("Username: ");
        fflush(stdout);
        if (fgets(opt.user, sizeof(opt.user), stdin) == NULL) {
            fprintf(stderr, "Error reading username\n");
            return 1;
        }
        size_t len = strlen(opt.user);
        if (len > 0 && opt.user[len-1] == '\n') {
            opt.user[len-1] = '\0';
        }
    }

    char password[128];
    if (read_password(password, sizeof(password)) != 0) {
        return 1;
    }

    int sock = connect_to_server(opt.addr, opt.port);
    if (sock < 0) {
        return 1;
    }

    printf("Connected to %s:%d\n", opt.addr, opt.port);

    char *auth_argv[3];
    auth_argv[0] = "AUTH";
    auth_argv[1] = opt.user;
    auth_argv[2] = password;

    if (send_resp_array(sock, 3, auth_argv) != 0) {
        fprintf(stderr, "Failed to send AUTH\n");
        close(sock);
        return 1;
    }

    if (read_and_print_resp(sock) != 0) {
        fprintf(stderr, "AUTH failed/connection closed\n");
        close(sock);
        return 1;
    }

    char line[1024];
    while (1) {
        printf("repa> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "quit") == 0) {
            char *q_argv[1];
            q_argv[0] = "QUIT";
            (void)send_resp_array(sock, 1, q_argv);
            (void)read_and_print_resp(sock);
            break;
        }

        char *cmd_argv[32];
        int cmd_argc = split_line(line, cmd_argv, 32);
        if (cmd_argc <= 0) {
            continue;
        }

        if (send_resp_array(sock, cmd_argc, cmd_argv) != 0) {
            fprintf(stderr, "Failed to send command\n");
            break;
        }

        if (read_and_print_resp(sock) != 0) {
            fprintf(stderr, "Failed to read response\n");
            break;
        }
    }

    close(sock);
    return 0;
}
