#include <psp2/appmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdio.h>
#include <string.h>

#define MOONLIGHT_DATA_DIR "ux0:data/moonlight"
#define MOONLIGHT_DEVICES_DIR MOONLIGHT_DATA_DIR "/devices"
#define FRONTEND_CONFIG_PATH MOONLIGHT_DATA_DIR "/interface.conf"

#define FRONTEND_LEGACY "legacy"
#define FRONTEND_BOREALIS "borealis"

static int path_exists(const char *path)
{
    SceIoStat stat;
    memset(&stat, 0, sizeof(stat));
    return sceIoGetstat(path, &stat) >= 0;
}

static int is_directory(const char *path)
{
    SceIoStat stat;
    memset(&stat, 0, sizeof(stat));
    return sceIoGetstat(path, &stat) >= 0 && SCE_S_ISDIR(stat.st_mode);
}

static void ensure_directory(const char *path)
{
    if (!is_directory(path))
        sceIoMkdir(path, 0777);
}

static int copy_file_if_missing(const char *source, const char *destination)
{
    if (path_exists(destination))
        return 0;

    SceUID input = sceIoOpen(source, SCE_O_RDONLY, 0);
    if (input < 0)
        return input;

    SceUID output = sceIoOpen(destination, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (output < 0)
    {
        sceIoClose(input);
        return output;
    }

    char buffer[8192];
    int result = 0;
    for (;;)
    {
        int read = sceIoRead(input, buffer, sizeof(buffer));
        if (read < 0)
        {
            result = read;
            break;
        }
        if (read == 0)
            break;

        int written = 0;
        while (written < read)
        {
            int count = sceIoWrite(output, buffer + written, read - written);
            if (count <= 0)
            {
                result = count < 0 ? count : -1;
                break;
            }
            written += count;
        }
        if (result < 0)
            break;
    }

    sceIoClose(output);
    sceIoClose(input);

    if (result < 0)
        sceIoRemove(destination);
    return result;
}

static void migrate_legacy_hosts(void)
{
    ensure_directory("ux0:data");
    ensure_directory(MOONLIGHT_DATA_DIR);
    ensure_directory(MOONLIGHT_DEVICES_DIR);

    SceUID root = sceIoDopen(MOONLIGHT_DATA_DIR);
    if (root < 0)
        return;

    SceIoDirent entry;
    while (1)
    {
        memset(&entry, 0, sizeof(entry));
        if (sceIoDread(root, &entry) <= 0)
            break;

        if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, "..") || !strcmp(entry.d_name, "devices"))
            continue;

        char source_dir[512];
        char source_ini[512];
        char destination_dir[512];
        snprintf(source_dir, sizeof(source_dir), "%s/%s", MOONLIGHT_DATA_DIR, entry.d_name);
        snprintf(source_ini, sizeof(source_ini), "%s/device.ini", source_dir);
        snprintf(destination_dir, sizeof(destination_dir), "%s/%s", MOONLIGHT_DEVICES_DIR, entry.d_name);

        if (!is_directory(source_dir) || !path_exists(source_ini))
            continue;

        ensure_directory(destination_dir);
        if (!is_directory(destination_dir))
            continue;

        SceUID host = sceIoDopen(source_dir);
        if (host < 0)
            continue;

        SceIoDirent host_entry;
        while (1)
        {
            memset(&host_entry, 0, sizeof(host_entry));
            if (sceIoDread(host, &host_entry) <= 0)
                break;
            if (!strcmp(host_entry.d_name, ".") || !strcmp(host_entry.d_name, ".."))
                continue;

            char source_file[512];
            char destination_file[512];
            snprintf(source_file, sizeof(source_file), "%s/%s", source_dir, host_entry.d_name);
            if (is_directory(source_file))
                continue;
            snprintf(destination_file, sizeof(destination_file), "%s/%s", destination_dir, host_entry.d_name);
            copy_file_if_missing(source_file, destination_file);
        }
        sceIoDclose(host);
    }

    sceIoDclose(root);
}

static int read_frontend(char *frontend, size_t size)
{
    FILE *file = fopen(FRONTEND_CONFIG_PATH, "r");
    if (!file)
        return 0;

    char line[32] = {0};
    const int ok = fgets(line, sizeof(line), file) != NULL;
    fclose(file);
    if (!ok)
        return 0;

    line[strcspn(line, "\r\n \t")] = '\0';
    if (strcmp(line, FRONTEND_LEGACY) && strcmp(line, FRONTEND_BOREALIS))
        return 0;

    snprintf(frontend, size, "%s", line);
    return 1;
}

static void write_frontend(const char *frontend)
{
    FILE *file = fopen(FRONTEND_CONFIG_PATH, "w");
    if (!file)
        return;
    fprintf(file, "%s\n", frontend);
    fclose(file);
}

static int launch_frontend(const char *frontend)
{
    const char *preferred = !strcmp(frontend, FRONTEND_BOREALIS)
        ? "app0:borealis.self"
        : "app0:legacy.self";
    const char *fallback = !strcmp(frontend, FRONTEND_BOREALIS)
        ? "app0:legacy.self"
        : "app0:borealis.self";

    int result = sceAppMgrLoadExec(preferred, NULL, NULL);
    if (result < 0)
    {
        write_frontend(!strcmp(frontend, FRONTEND_BOREALIS) ? FRONTEND_LEGACY : FRONTEND_BOREALIS);
        result = sceAppMgrLoadExec(fallback, NULL, NULL);
    }
    return result;
}

int main(void)
{
    migrate_legacy_hosts();

    char frontend[16] = {0};
    if (!read_frontend(frontend, sizeof(frontend)))
    {
        snprintf(frontend, sizeof(frontend), "%s", FRONTEND_LEGACY);
        write_frontend(frontend);
    }

    const int result = launch_frontend(frontend);
    FILE *log = fopen(MOONLIGHT_DATA_DIR "/launcher.log", "w");
    if (log)
    {
        fprintf(log, "Unable to launch Moonlight frontend: 0x%08X\n", result);
        fclose(log);
    }
    return 1;
}
