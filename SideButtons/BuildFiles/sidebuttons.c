#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <ctype.h>

typedef struct {
    int forward;
    int backward;
} ButtonState;

typedef struct {
    char forward_pattern[9];
    char backward_pattern[9];
    int device_identifier;
    int forward_index;
    int backward_index;
} Config;

ButtonState button_state = {0, 0};
Config config;

void trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

int read_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open config file %s\n", filename);
        return 0;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        char *equals = strchr(line, '=');
        if (!equals) continue;
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        trim_whitespace(key);
        trim_whitespace(value);
        if (strcasecmp(key, "SIDE") == 0) {
            if (strlen(value) == 8) {
                strcpy(config.forward_pattern, value);
            }
        } else if (strcasecmp(key, "EXTRA") == 0) {
            if (strlen(value) == 8) {
                strcpy(config.backward_pattern, value);
            }
        } else if (strcasecmp(key, "DEVICE") == 0) {
            config.device_identifier = atoi(value);
        }
    }
    fclose(file);

    // Detect first non-zero hex digit indices
    config.forward_index = 0;
    while (config.forward_index < 8 && config.forward_pattern[config.forward_index] == '0') {
        config.forward_index++;
    }
    config.backward_index = 0;
    while (config.backward_index < 8 && config.backward_pattern[config.backward_index] == '0') {
        config.backward_index++;
    }

    return 1;
}

int extract_bytes(const char *line, char *result) {
    const char *ptr = strchr(line, '=');
    if (ptr == NULL) {
        return 0;
    }
    ptr++;
    while (*ptr == ' ' || *ptr == '\t') {
        ptr++;
    }
    int i;
    for (i = 0; i < 8; i++) {
        if ((ptr[i] >= '0' && ptr[i] <= '9') || 
            (ptr[i] >= 'a' && ptr[i] <= 'f') || 
            (ptr[i] >= 'A' && ptr[i] <= 'F')) {
            result[i] = ptr[i];
        } else {
            return 0;
        }
    }
    result[8] = '\0';
    return 1;
}

int extract_number_before_equals(const char *line) {
    const char *equals_ptr = strchr(line, '=');
    if (equals_ptr == NULL) {
        return -1; 
    }
    const char *ptr = equals_ptr - 1;
    while (ptr >= line && (*ptr == ' ' || *ptr == '\t')) {
        ptr--;
    }
    const char *number_end = ptr;
    while (ptr >= line && isdigit(*ptr)) {
        ptr--;
    }
    const char *number_start = ptr + 1;
    int number_length = number_end - number_start + 1;
    if (number_length <= 0) {
        return -1; 
    }
    char number_str[32];
    strncpy(number_str, number_start, number_length);
    number_str[number_length] = '\0';
    return atoi(number_str);
}

int hex_to_dec(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int hex_chars_bit_check(char hex_char, char pattern_char) {
    int hex_val = hex_to_dec(hex_char);
    int pattern_val = hex_to_dec(pattern_char);
    if (hex_val == -1 || pattern_val == -1) return 0;
    return (hex_val & pattern_val) ? 1 : 0;
}

int main() {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return 1;
    }
    exe_path[len] = '\0';
    char *dir = dirname(exe_path);
    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/device.conf", dir);
    if (!read_config(config_path)) {
        return 1;
    }

    system("pkill -x \"ydotoold\"");
    system("ydotoold &");

    int fd = open("/sys/kernel/debug/usb/usbmon/1u", O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Error opening usbmon: %s\n", strerror(errno));
        return 1;
    }

    char buffer[4096];
    ssize_t bytes_read;
    char line[256];
    size_t line_pos = 0;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n' || line_pos >= sizeof(line) - 1) {
                line[line_pos] = '\0';
                char hex_bytes[9];
                if (extract_bytes(line, hex_bytes) && 
                    (extract_number_before_equals(line) == config.device_identifier)) {

                    if (hex_chars_bit_check(hex_bytes[config.forward_index], config.forward_pattern[config.forward_index]) && (!button_state.forward)) {
                        system("ydotool click 0x44");
                        button_state.forward = 1;
                    } else if ((!hex_chars_bit_check(hex_bytes[config.forward_index], config.forward_pattern[config.forward_index])) && button_state.forward) {
                        system("ydotool click 0x84");
                        button_state.forward = 0;
                    }

                    if (hex_chars_bit_check(hex_bytes[config.backward_index], config.backward_pattern[config.backward_index]) && (!button_state.backward)) {
                        system("ydotool click 0x43");
                        button_state.backward = 1;
                    } else if ((!hex_chars_bit_check(hex_bytes[config.backward_index], config.backward_pattern[config.backward_index])) && button_state.backward) {
                        system("ydotool click 0x83");
                        button_state.backward = 0;
                    }
                }
                line_pos = 0;
            } else {
                line[line_pos++] = buffer[i];
            }
        }
    }

    close(fd);
    return 0;
}
