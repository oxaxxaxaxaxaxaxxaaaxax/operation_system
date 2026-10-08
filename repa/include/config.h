#pragma once

#include <stddef.h>
#include "logger.h"
#include "constants.h"

#define REPA_MAX_PATH_LEN 256

typedef struct {
    int port;

    char default_user[REPA_MAX_USER_LEN];
    char default_password[REPA_MAX_PASS_LEN];

    size_t max_memory_mb;
    int default_ttl;
    int workers;

    log_level_t log_level;
    char log_output[REPA_MAX_PATH_LEN];

} repa_config;

void config_set_defaults(repa_config *cfg);

int config_load_file(repa_config *cfg, const char *path);

int config_apply_cli_args(repa_config *cfg,int argc, char **argv,
    const char **out_config_path,int *out_show_help);
