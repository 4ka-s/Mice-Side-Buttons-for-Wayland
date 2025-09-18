#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <libgen.h>
#include <limits.h>

#define USBMON_PATH "/sys/kernel/debug/usb/usbmon/1u"
#define MAX_LINE 2048
#define MAX_BYTES 64

int parse_payload_bytes(const char *line, unsigned char bytes[], int max_bytes) {
    const char *eq = strchr(line, '=');
    if (!eq) return 0;
    const char *p = eq + 1;
    int cnt = 0;
    while (*p && cnt < max_bytes) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char *q = p;
        while (*q && !isspace((unsigned char)*q)) q++;
        const char *r = p;
        while (r + 1 < q && cnt < max_bytes) {
            if (isxdigit((unsigned char)r[0]) && isxdigit((unsigned char)r[1])) {
                unsigned int val;
                if (sscanf(r, "%2x", &val) == 1) {
                    bytes[cnt++] = (unsigned char)val;
                } else break;
                r += 2;
            } else break;
        }
        p = q;
    }
    return cnt;
}

int extract_number_before_equal(const char *line) {
    const char *eq = strchr(line, '=');
    if (!eq) return -1;
    const char *p = eq;
    while (p > line && isspace((unsigned char)*(p - 1))) p--;
    const char *start = p;
    while (start > line && !isspace((unsigned char)*(start - 1))) start--;
    size_t len = (size_t)(p - start);
    if (len == 0 || len >= 64) return -1;
    char tok[64];
    strncpy(tok, start, len);
    tok[len] = '\0';
    return atoi(tok);
}

int capture_one_reading(unsigned char out_bytes[], int *out_count, int *out_num_before) {
    FILE *fp = fopen(USBMON_PATH, "r");
    if (!fp) {
        perror("fopen " USBMON_PATH);
        return 0;
    }
    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), fp)) {
        perror("fgets");
        fclose(fp);
        return 0;
    }
    fclose(fp);
    *out_count = parse_payload_bytes(line, out_bytes, MAX_BYTES);
    *out_num_before = extract_number_before_equal(line);
    return 1;
}

uint32_t compute_8digit_int(unsigned char held[], unsigned char released[], int count, int *out_location) {
    int max_cmp = count;
    for (int i = 0; i < max_cmp; ++i) {
        if (held[i] != 0 && released[i] == 0) {
            unsigned char orig_byte = held[i];
            int location = i + 1; 
            *out_location = location;
            if (location >= 1 && location <= 4) {
                int shift_bits = (4 - location) * 8;
                uint32_t result = ((uint32_t)orig_byte) << shift_bits;
                return result;
            } else {
                return (uint32_t)orig_byte;
            }
        }
    }
    *out_location = -1;
    return 0;
}

uint32_t do_button_cycle(const char *name, int *num_before) {
    unsigned char held[MAX_BYTES], released[MAX_BYTES];
    int held_count = 0, released_count = 0;
    int location = -1;
    if (!capture_one_reading(held, &held_count, num_before)) {
        fprintf(stderr, "Failed to capture first reading (%s)\n", name);
        exit(1);
    }
    if (!capture_one_reading(released, &released_count, num_before)) {
        fprintf(stderr, "Failed to capture release reading (%s)\n", name);
        exit(1);
    }
    uint32_t value = compute_8digit_int(held, released, (held_count < released_count) ? held_count : released_count, &location);
    return value;
}

int main(void) {
    for (int i = 3; i >= 1; --i) {
        fflush(stdout);
        sleep(1);
    }
    int num_before_forward = -1, num_before_backward = -1;
    uint32_t forward_val = do_button_cycle("FORWARD", &num_before_forward);
    uint32_t backward_val = do_button_cycle("BACKWARD", &num_before_backward);
    int device_number = num_before_forward;
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        perror("readlink");
        return 1;
    }
    exe_path[len] = '\0';
    char *dir = dirname(exe_path);
    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/device.conf", dir);
    FILE *conf = fopen(config_path, "w");
    if (!conf) {
        perror("fopen device.conf");
        return 1;
    }
    fprintf(conf, "SIDE=%08X\n", forward_val);
    fprintf(conf, "EXTRA=%08X\n", backward_val);
    fprintf(conf, "DEVICE=%d\n", device_number);
    fclose(conf);
    return 0;
}
