#include "../src/config/env_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void create_dummy_env_file(const char *filename) {
    FILE *f = fopen(filename, "w");
    assert(f != NULL);
    fprintf(f, "\n");
    fprintf(f, "# This is a comment line\n");
    fprintf(f, "   # Another comment with spaces\n");
    fprintf(f, "APP_ENV=test_env\n");
    fprintf(f, "APP_PORT=9090\n");
    fprintf(f, "   SPACED_KEY   =   spaced_value   \n");
    fprintf(f, "DOUBLE_QUOTED=\"hello world\"\n");
    fprintf(f, "SINGLE_QUOTED='single world'\n");
    fprintf(f, "OVERRIDDEN_VAR=from_file\n");
    fprintf(f, "EMPTY_LINE_AFTER=\n");
    fprintf(f, "\n");
    fclose(f);
}

int main() {
    const char *test_env_path = "dummy_test.env";
    int load_res;

#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("OVERRIDDEN_VAR", "pre_existing_value");
#else
    setenv("OVERRIDDEN_VAR", "pre_existing_value", 1);
#endif

    create_dummy_env_file(test_env_path);

    load_res = load_env_file("non_existent_file.env");
    if (load_res != -1) {
        printf("FAILED: Non-existent file should return -1\n");
        return 1;
    }
    printf("PASSED: Missing configuration file returns -1\n");

    load_res = load_env_file(test_env_path);
    if (load_res != 0) {
        printf("FAILED: Valid file should return 0\n");
        return 1;
    }
    printf("PASSED: Valid configuration file loaded successfully\n");

    if (strcmp(get_env_value("APP_ENV", ""), "test_env") != 0) {
        printf("FAILED: APP_ENV mismatch\n");
        return 1;
    }
    printf("PASSED: Standard key-value pair\n");

    if (get_env_int("APP_PORT", 0) != 9090) {
        printf("FAILED: APP_PORT mismatch\n");
        return 1;
    }
    printf("PASSED: Integer conversion\n");

    if (strcmp(get_env_value("SPACED_KEY", ""), "spaced_value") != 0) {
        printf("FAILED: SPACED_KEY whitespace trim mismatch\n");
        return 1;
    }
    printf("PASSED: Whitespace trimmed around key and value\n");

    if (strcmp(get_env_value("DOUBLE_QUOTED", ""), "hello world") != 0) {
        printf("FAILED: DOUBLE_QUOTED quote stripping mismatch\n");
        return 1;
    }
    printf("PASSED: Double quote stripping\n");

    if (strcmp(get_env_value("SINGLE_QUOTED", ""), "single world") != 0) {
        printf("FAILED: SINGLE_QUOTED quote stripping mismatch\n");
        return 1;
    }
    printf("PASSED: Single quote stripping\n");

    if (strcmp(get_env_value("OVERRIDDEN_VAR", ""), "pre_existing_value") != 0) {
        printf("FAILED: Existing environment variable was overwritten\n");
        return 1;
    }
    printf("PASSED: Existing system environment variable preserved\n");

    if (strcmp(get_env_value("NON_EXISTENT_KEY", "default_val"), "default_val") != 0) {
        printf("FAILED: Default value fallback failed\n");
        return 1;
    }
    printf("PASSED: Default value fallback\n");

    remove(test_env_path);
    printf("ALL CONFIG LOADER TESTS SUCCEEDED.\n");
    return 0;
}
