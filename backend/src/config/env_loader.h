#ifndef ENV_LOADER_H
#define ENV_LOADER_H

int load_env_file(const char *filepath);
const char *get_env_value(const char *key, const char *default_value);
int get_env_int(const char *key, int default_value);

#endif
