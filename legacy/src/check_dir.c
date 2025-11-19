#include <psp2/io/stat.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "config.h"
#include "check_dir.h"

// Intenta crear la carpeta en varias rutas y retorna la ruta exitosa en out_path
// Si hay errores, los registra en ux0:data/moonlight.log
// out_path y out_key_dir deben tener espacio suficiente (>=MOONLIGHT_PATH_MAX)
void check_and_create_moonlight_dir(char* out_path, char* out_key_dir) {
    #ifdef USE_DIR_UMA0
    const char* test_paths[] = {
        "uma0:data/moonlight"
    };
    int path_count = 1;
    #else
    const char* test_paths[] = {
        "ux0:data/moonlight",
        "ux0:moonlight",
        "uma0:data/moonlight"
    };
    int path_count = sizeof(test_paths)/sizeof(test_paths[0]);
    #endif
    int success_idx = -1;
    int mkdir_result = 0;
    char log_path[MOONLIGHT_PATH_MAX];
    if (success_idx >= 0) {
        // Extraer el almacenamiento base (ux0:data o uma0:data)
        const char* base = test_paths[success_idx];
        if (strncmp(base, "uma0:data", 9) == 0) {
            snprintf(log_path, MOONLIGHT_PATH_MAX, "uma0:data/moonlight.log");
        } else {
            snprintf(log_path, MOONLIGHT_PATH_MAX, "ux0:data/moonlight.log");
        }
        log_path[MOONLIGHT_PATH_MAX-1] = '\0';
    } else {
        snprintf(log_path, MOONLIGHT_PATH_MAX, "ux0:data/moonlight.log");
        log_path[MOONLIGHT_PATH_MAX-1] = '\0';
    }
    int ux0_failed = 0;
    for (int i = 0; i < path_count; i++) {
        mkdir_result = sceIoMkdir(test_paths[i], 0777);
        if (mkdir_result >= 0 || mkdir_result == 0x80010011) { // 0x80010011 = ya existe
            success_idx = i;
            vita_debug_log("[Moonlight] Folder created/existing: %s", test_paths[i]);
            break;
        } else {
            if (i == 0 && strncmp(test_paths[i], "ux0:data", 8) == 0) {
                ux0_failed = 1;
            }
        }
    }
    FILE* test_log = NULL;
    if (success_idx >= 0) {
        snprintf(out_key_dir, MOONLIGHT_PATH_MAX, "%s/", test_paths[success_idx]);
        out_key_dir[MOONLIGHT_PATH_MAX-1] = '\0';
        snprintf(out_path, MOONLIGHT_PATH_MAX, "%s/moonlight.conf", test_paths[success_idx]);
        out_path[MOONLIGHT_PATH_MAX-1] = '\0';
        // Si se creó en uma0 y ux0 falló, crear el log en uma0:data/moonlight.log con mensaje en inglés
        if (strncmp(test_paths[success_idx], "uma0:data", 9) == 0 && ux0_failed) {
            snprintf(log_path, MOONLIGHT_PATH_MAX, "uma0:data/moonlight.log");
            log_path[MOONLIGHT_PATH_MAX-1] = '\0';
            test_log = fopen(log_path, "w");
            if (test_log) {
                fprintf(test_log, "[Moonlight] ux0:data/moonlight could not be created. Folder was created in uma0:data/moonlight instead.\n");
                fclose(test_log);
            }
        }
    } else {
        snprintf(out_key_dir, MOONLIGHT_PATH_MAX, "ux0:data/");
        out_key_dir[MOONLIGHT_PATH_MAX-1] = '\0';
        snprintf(out_path, MOONLIGHT_PATH_MAX, "ux0:data/moonlight.conf");
        out_path[MOONLIGHT_PATH_MAX-1] = '\0';
        snprintf(log_path, MOONLIGHT_PATH_MAX, "ux0:data/moonlight.log");
        log_path[MOONLIGHT_PATH_MAX-1] = '\0';
        test_log = fopen(log_path, "w");
        if (test_log) {
            fprintf(test_log, "[Moonlight] No folder could be created, using ux0:data as fallback.\n");
            fclose(test_log);
        }
    }
    // Crear el archivo moonlight.conf si no existe
    FILE* conf_file = fopen(out_path, "a");
    if (conf_file) fclose(conf_file);
    if (test_log) fclose(test_log);

    // Guardar solo key_dir en moonlight.conf
    FILE* conf_save = fopen(out_path, "r+");
    if (conf_save) {
        // Buscar si ya existe la línea key_dir
        char line[512];
        int found = 0;
        while (fgets(line, sizeof(line), conf_save)) {
            if (strncmp(line, "key_dir = ", 10) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fseek(conf_save, 0, SEEK_END);
            fprintf(conf_save, "key_dir = %s\n", out_key_dir);
        }
        fclose(conf_save);
    }
}
