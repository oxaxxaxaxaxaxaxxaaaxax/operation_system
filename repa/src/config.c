#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>

static char *trim(char *s){
    if (s == NULL) {
        return s;
    }

    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}


void config_set_defaults(repa_config *cfg){
    cfg->port = 6380;

    strncpy(cfg->default_user, "admin", REPA_MAX_USER_LEN);
    strncpy(cfg->default_password, "admin", REPA_MAX_PASS_LEN);

    cfg->default_user[REPA_MAX_USER_LEN - 1] = '\0';
    cfg->default_password[REPA_MAX_PASS_LEN - 1] = '\0';

    cfg->max_memory_mb = 256;
    cfg->default_ttl = 0;
    cfg->workers = 4;

    cfg->log_level = LOG_LEVEL_INFO;

    strncpy(cfg->log_output, "repa.log", REPA_MAX_PATH_LEN);
    cfg->log_output[REPA_MAX_PATH_LEN - 1] = '\0';
}


int config_load_file(repa_config *cfg, const char *path){
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return -1; 
    }

    char line[512];
    int line_counter = 0;

    while (fgets(line, sizeof(line), f)) {
        line_counter++;
        char *p = trim(line);

        if (*p == '#' || *p == ';' || *p == '\0') {
            continue;
        }

        char *eq = strchr(p, '=');
        if (eq == NULL) {
            fprintf(stderr, "Config warning %s:%d: no '='\n", path, line_counter);
            continue;
        }

        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);

        if (strcmp(key, "port") == 0) {
            cfg->port = atoi(val);

        } else if (strcmp(key, "default_user") == 0) {
            strncpy(cfg->default_user, val, REPA_MAX_USER_LEN);

        } else if (strcmp(key, "default_password") == 0) {
            strncpy(cfg->default_password, val, REPA_MAX_PASS_LEN);

        } else if (strcmp(key, "max_memory_mb") == 0) {
            cfg->max_memory_mb = strtoull(val, NULL, 10);

        } else if (strcmp(key, "default_ttl") == 0) {
            cfg->default_ttl = atoi(val);

        } else if (strcmp(key, "workers") == 0) {
            cfg->workers = atoi(val);

        } else if (strcmp(key, "log_level") == 0) {
            if (strcmp(val, "debug") == 0){
                cfg->log_level = LOG_LEVEL_DEBUG;
            }
            else if (strcmp(val, "info") == 0) {
                cfg->log_level = LOG_LEVEL_INFO;
            }
            else if (strcmp(val, "warn") == 0 || strcmp(val, "warning") == 0) {
                cfg->log_level = LOG_LEVEL_WARN;
            }
            else if (strcmp(val, "error") == 0) {
                cfg->log_level = LOG_LEVEL_ERROR;
            }

        } else if (strcmp(key, "log_output") == 0) {
            strncpy(cfg->log_output, val, REPA_MAX_PATH_LEN);

        } else {
            fprintf(stderr, "Config warning ! unknown key '%s'\n", key);
        }
    }

    fclose(f);
    return 0;
}


static void print_help(void){
    printf("Usage [options]\n");
    printf("  --port <num>\n");
    printf("  --config <path>\n");
    printf("  --verbose\n");
    printf("  --max-memory-mb <num>\n");
    printf("  --workers <num>\n");
    printf("  --default-ttl <sec>\n");
    printf("  --help\n");
}

int config_apply_cli_args(repa_config *cfg,int argc, char **argv,char **out_config_path,int *out_show_help){
    if (out_show_help) *out_show_help = 0;
    if (out_config_path) *out_config_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            if (out_show_help) *out_show_help = 1;
            return 1;
        }
        else if (strcmp(argv[i], "--verbose") == 0) {
            cfg->log_level = LOG_LEVEL_DEBUG;
        }
        else if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--port requires a value\n");
                return -1;
            }
            i++;
            cfg->port = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--config requires a path\n");
                return -1;
            }
            i++;
            if (out_config_path) *out_config_path = argv[i];
        }
        else if (strcmp(argv[i], "--max-memory-mb") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--max-memory-mb requires a value\n");
                return -1;
            }
            i++;
            cfg->max_memory_mb = atoll(argv[i]);
        }
        else if (strcmp(argv[i], "--workers") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--workers requires a value\n");
                return -1;
            }
            i++;
            cfg->workers = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--default-ttl") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--default-ttl requires a value\n");
                return -1;
            }
            i++;
            cfg->default_ttl = atoi(argv[i]);
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    return 0; 
}
