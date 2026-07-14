#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

static int run(const char *cmd) {
    printf("  $ %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0)
        fprintf(stderr, "  ! command failed (%d)\n", ret);
    return ret;
}

static char **read_lines(const char *path, int *count) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char **lines = NULL;
    int n = 0;
    char buf[16384];
    while (fgets(buf, sizeof(buf), f)) {
        lines = realloc(lines, sizeof(char *) * (n + 1));
        lines[n++] = strdup(buf);
    }
    fclose(f);
    *count = n;
    return lines;
}

static int write_lines(const char *path, char **lines, int count) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < count; i++)
        fputs(lines[i], f);
    fclose(f);
    return 0;
}

static void free_lines(char **lines, int count) {
    for (int i = 0; i < count; i++)
        free(lines[i]);
    free(lines);
}

static int contains(const char *s, const char *sub) {
    return strstr(s, sub) != NULL;
}

static char *insert_before_tag_close(char *line, const char *attr) {
    char *gt = strrchr(line, '>');
    if (!gt) return line;
    char *pos = gt;
    if (gt > line && gt[-1] == '/') pos = gt - 1;
    int attr_len = strlen(attr);
    int line_len = strlen(line);
    int offset = pos - line;
    char *nl = malloc(line_len + attr_len + 1);
    memcpy(nl, line, offset);
    memcpy(nl + offset, attr, attr_len);
    strcpy(nl + offset + attr_len, pos);
    free(line);
    return nl;
}

static char *replace_const_val(char *line) {
    char *comma = strchr(line, ',');
    if (!comma) return line;
    comma++;
    while (*comma == ' ') comma++;
    long val = strtol(comma, NULL, 0);
    if (val == 0) return line;
    char *end = comma;
    while (*end && *end != '\n' && *end != '\r' && *end != ' ') end++;
    int prefix = comma - line;
    int suffix_len = strlen(end);
    char *nl = malloc(prefix + 4 + suffix_len + 1);
    memcpy(nl, line, prefix);
    memcpy(nl + prefix, "0x0", 3);
    strcpy(nl + prefix + 3, end);
    free(line);
    return nl;
}

static int patch_manifest(const char *path) {
    int n;
    char **lines = read_lines(path, &n);
    if (!lines) return -1;

    for (int i = 0; i < n; i++) {
        char *p;
        if ((p = strstr(lines[i], "android:sharedUserId=\""))) {
            char *start = p;
            char *val = p + strlen("android:sharedUserId=\"");
            char *end = strchr(val, '"');
            if (end) end++;
            if (start > lines[i] && start[-1] == ' ') start--;
            memmove(start, end, strlen(end) + 1);
        }
        if ((p = strstr(lines[i], "platformBuildVersionCode=\""))) {
            char *val = p + strlen("platformBuildVersionCode=\"");
            char *end = strchr(val, '"');
            if (end) {
                memmove(val + 2, val + (end - val), strlen(end) + 1);
                memcpy(val, "24", 2);
            }
        }
        if ((p = strstr(lines[i], "platformBuildVersionName=\""))) {
            char *val = p + strlen("platformBuildVersionName=\"");
            char *end = strchr(val, '"');
            if (end) {
                memmove(val + 3, val + (end - val), strlen(end) + 1);
                memcpy(val, "7.0", 3);
            }
        }
        if (contains(lines[i], "<application ") && !contains(lines[i], "/>")) {
            if (!contains(lines[i], "android:requestLegacyExternalStorage"))
                lines[i] = insert_before_tag_close(lines[i], " android:requestLegacyExternalStorage=\"true\"");
            if (!contains(lines[i], "android:usesCleartextTraffic"))
                lines[i] = insert_before_tag_close(lines[i], " android:usesCleartextTraffic=\"true\"");
            if (!contains(lines[i], "android:extractNativeLibs"))
                lines[i] = insert_before_tag_close(lines[i], " android:extractNativeLibs=\"true\"");
        }
    }

    const char *tags[] = {"<activity ", "<receiver ", "<service ", "<provider "};
    const char *closes[] = {"</activity>", "</receiver>", "</service>", "</provider>"};
    for (int i = 0; i < n; i++) {
        for (int t = 0; t < 4; t++) {
            if (!contains(lines[i], tags[t]) || contains(lines[i], "android:exported"))
                continue;
            int has_filter = 0;
            for (int j = i + 1; j < n; j++) {
                if (contains(lines[j], closes[t])) break;
                if (contains(lines[j], "<intent-filter")) {
                    has_filter = 1;
                    break;
                }
            }
            if (has_filter)
                lines[i] = insert_before_tag_close(lines[i], " android:exported=\"true\"");
        }
    }

    int rc = write_lines(path, lines, n);
    free_lines(lines, n);
    printf("  patched AndroidManifest.xml\n");
    return rc;
}

static int patch_apktool_yml(const char *path) {
    int n;
    char **lines = read_lines(path, &n);
    if (!lines) return -1;
    for (int i = 0; i < n; i++) {
        if (contains(lines[i], "targetSdkVersion:")) {
            char *colon = strchr(lines[i], ':');
            if (colon) {
                colon++;
                while (*colon == ' ') colon++;
                char *end = colon;
                while (*end && *end != '\n' && *end != '\r') end++;
                int prefix = colon - lines[i];
                int suffix_len = strlen(end);
                char *nl = malloc(prefix + 3 + suffix_len + 1);
                memcpy(nl, lines[i], prefix);
                memcpy(nl + prefix, "24", 2);
                strcpy(nl + prefix + 2, end);
                free(lines[i]);
                lines[i] = nl;
            }
        }
    }
    int rc = write_lines(path, lines, n);
    free_lines(lines, n);
    printf("  patched apktool.yml\n");
    return rc;
}

static int patch_smali_file(const char *path) {
    int n;
    char **lines = read_lines(path, &n);
    if (!lines) return -1;
    int modified = 0;

    for (int i = 0; i < n; i++) {
        if (!contains(lines[i], "getSharedPreferences(Ljava/lang/String;I)") &&
            !contains(lines[i], "openFileOutput(Ljava/lang/String;I)"))
            continue;

        char *brace = strchr(lines[i], '{');
        char *brace_end = strchr(lines[i], '}');
        if (!brace || !brace_end) continue;

        char *last_comma = NULL;
        for (char *p = brace; p < brace_end; p++)
            if (*p == ',') last_comma = p;
        if (!last_comma) continue;

        char mode_reg[32];
        char *s = last_comma + 1;
        while (*s == ' ') s++;
        int len = brace_end - s;
        if (len <= 0 || len >= 32) continue;
        memcpy(mode_reg, s, len);
        mode_reg[len] = '\0';

        char pat4[64], pat16[64];
        snprintf(pat4, sizeof(pat4), "const/4 %s,", mode_reg);
        snprintf(pat16, sizeof(pat16), "const/16 %s,", mode_reg);

        for (int j = i - 1; j >= 0 && j >= i - 20; j--) {
            if (lines[j][0] == '.') break;
            if (contains(lines[j], pat4) || contains(lines[j], pat16)) {
                lines[j] = replace_const_val(lines[j]);
                modified = 1;
                break;
            }
        }
    }

    if (modified) {
        write_lines(path, lines, n);
        printf("  patched shared-prefs modes: %s\n", path);
    }
    free_lines(lines, n);
    return 0;
}

static int patch_smali_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (e->d_type == DT_DIR)
            patch_smali_dir(path);
        else if (e->d_type == DT_REG && strstr(e->d_name, ".smali"))
            patch_smali_file(path);
    }
    closedir(d);
    return 0;
}

static int find_file_recursive(const char *dir, const char *name_part, char *out, size_t out_sz) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (e->d_type == DT_DIR) {
            if (find_file_recursive(path, name_part, out, out_sz) == 0) {
                closedir(d);
                return 0;
            }
        } else if (e->d_type == DT_REG && strstr(e->d_name, name_part)) {
            strncpy(out, path, out_sz - 1);
            out[out_sz - 1] = '\0';
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return -1;
}

static int patch_sensor_listener(const char *path) {
    int n;
    char **lines = read_lines(path, &n);
    if (!lines) return -1;
    int modified = 0;

    const char *markers[] = {"\"its1 = ", "\"its2 = "};
    for (int m = 0; m < 2; m++) {
        for (int i = 0; i < n; i++) {
            if (!contains(lines[i], markers[m]))
                continue;
            for (int j = i + 1; j < n && j < i + 10; j++) {
                if (contains(lines[j], "aget")) {
                    char *aget = strstr(lines[j], "aget");
                    char *p = aget + 4;
                    while (*p == ' ') p++;
                    char *comma = strchr(p, ',');
                    if (!comma) break;
                    int reg_len = comma - p;
                    char reg[32];
                    if (reg_len >= 32) break;
                    memcpy(reg, p, reg_len);
                    reg[reg_len] = '\0';
                    char newline[128];
                    snprintf(newline, sizeof(newline), "    const/4 %s, 0x0\n", reg);
                    free(lines[j]);
                    lines[j] = strdup(newline);
                    modified = 1;
                    break;
                }
            }
            break;
        }
    }

    if (modified) {
        write_lines(path, lines, n);
        printf("  patched sensor listener: %s\n", path);
    }
    free_lines(lines, n);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-to-apk>\n", argv[0]);
        return 1;
    }

    const char *apk = argv[1];
    struct stat st;
    if (stat(apk, &st) != 0) {
        fprintf(stderr, "Error: cannot find %s\n", apk);
        return 1;
    }

    char work_dir[] = "/tmp/picovr_patch_XXXXXX";
    if (!mkdtemp(work_dir)) {
        fprintf(stderr, "Error: cannot create temp dir\n");
        return 1;
    }

    char cmd[PATH_MAX * 3];
    char path[PATH_MAX];

    printf("[1/7] Decompiling APK with apktool...\n");
    snprintf(cmd, sizeof(cmd), "apktool d -f -o %s \"%s\" 2>&1", work_dir, apk);
    if (run(cmd)) return 1;

    printf("[2/7] Patching AndroidManifest.xml...\n");
    snprintf(path, sizeof(path), "%s/AndroidManifest.xml", work_dir);
    patch_manifest(path);

    printf("[3/7] Patching apktool.yml (targetSdkVersion)...\n");
    snprintf(path, sizeof(path), "%s/apktool.yml", work_dir);
    patch_apktool_yml(path);

    printf("[4/7] Patching smali (MODE_WORLD_READABLE + sensor crash)...\n");
    DIR *d = opendir(work_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strncmp(e->d_name, "smali", 5) != 0 || e->d_type != DT_DIR)
                continue;
            snprintf(path, sizeof(path), "%s/%s", work_dir, e->d_name);
            patch_smali_dir(path);
            char sensor_path[PATH_MAX];
            if (find_file_recursive(path, "MySensorEventListener", sensor_path, sizeof(sensor_path)) == 0)
                patch_sensor_listener(sensor_path);
        }
        closedir(d);
    }

    printf("[5/7] Rebuilding APK with apktool...\n");
    char unsigned_apk[PATH_MAX];
    snprintf(unsigned_apk, sizeof(unsigned_apk), "%s/unsigned.apk", work_dir);
    snprintf(cmd, sizeof(cmd), "apktool b %s -o %s 2>&1", work_dir, unsigned_apk);
    if (run(cmd)) return 1;

    printf("[6/7] Generating debug keystore (if needed)...\n");
    if (access("debug.keystore", F_OK) != 0) {
        snprintf(cmd, sizeof(cmd),
            "keytool -genkey -v -keystore debug.keystore -alias androiddebugkey "
            "-keyalg RSA -keysize 2048 -validity 10000 -storepass android -keypass android "
            "-dname \"CN=Android Debug,O=Android,C=US\" 2>&1");
        run(cmd);
    }

    printf("[7/7] Signing APK...\n");
    char output_apk[PATH_MAX];
    snprintf(output_apk, sizeof(output_apk), "%s", apk);
    char *dot = strrchr(output_apk, '.');
    if (dot && !strcmp(dot, ".apk")) *dot = '\0';
    strcat(output_apk, "_patched.apk");

    snprintf(cmd, sizeof(cmd),
        "apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android "
        "--out \"%s\" %s 2>&1", output_apk, unsigned_apk);
    if (run(cmd)) return 1;

    printf("\nDone: %s\n", output_apk);
    return 0;
}
