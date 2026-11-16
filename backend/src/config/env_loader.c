#include "env_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) {
        str++;
    }
    if (*str == '\0') {
        return str;
    }
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    return str;
}

static void strip_surrounding_quotes(char *str) {
    size_t len = strlen(str);
    if (len >= 2) {
        if ((str[0] == '"' && str[len - 1] == '"') || (str[0] == '\'' && str[len - 1] == '\'')) {
            memmove(str, str + 1, len - 2);
            str[len - 2] = '\0';
        }
    }
}

static int set_env_variable(const char *key, const char *value) {
    if (getenv(key) != NULL) {
        return 0;
    }
#if defined(_WIN32) || defined(_WIN64)
    return _putenv_s(key, value);
#else
    return setenv(key, value, 0);
#endif
}

int load_env_file(const char *filepath) {
    FILE *file;
    char line[1024];

    if (!filepath) {
        filepath = ".env";
    }

    file = fopen(filepath, "r");
    if (!file && strcmp(filepath, ".env") == 0) {
        file = fopen("../.env", "r");
    }
    if (!file) {
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        char *trimmed = trim_whitespace(line);
        char *equals_pos;
        char *key;
        char *value;

        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        equals_pos = strchr(trimmed, '=');
        if (!equals_pos) {
            continue;
        }

        *equals_pos = '\0';
        key = trim_whitespace(trimmed);
        value = trim_whitespace(equals_pos + 1);

        if (strlen(key) == 0) {
            continue;
        }

        strip_surrounding_quotes(value);
        set_env_variable(key, value);
    }

    fclose(file);
    return 0;
}

const char *get_env_value(const char *key, const char *default_value) {
    const char *val = getenv(key);
    if (!val || strlen(val) == 0) {
        return default_value;
    }
    return val;
}

int get_env_int(const char *key, int default_value) {
    const char *val = getenv(key);
    if (!val || strlen(val) == 0) {
        return default_value;
    }
    return atoi(val);
}
