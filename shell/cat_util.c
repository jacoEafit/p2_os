#include "shell.h"
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <time.h>

/**
 * ====================================================================================
 * COMANDO: saludar
 * ====================================================================================
 * Saluda al usuario autenticado en el sistema.
 * 
 * Syscalls explicadas:
 * 1. getuid(2): Obtiene el Identificador de Usuario (UID) real del proceso.
 *    - Retorna el entero que representa la cuenta (e.g. 0 para root, 1000 para usuarios).
 * 
 * Funciones de biblioteca explicadas:
 * - getpwuid(3): Lee la base de datos de usuarios (usualmente el archivo del sistema `/etc/passwd`)
 *   para traducir el UID numérico a una estructura struct passwd con el nombre de usuario de login.
 */
int cmd_saludar(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 1. LLAMADA AL SISTEMA: getuid */
    LOG_SYSCALL("getuid", "");
    uid_t uid = getuid();
    LOG_SYSCALL_RESULT(uid); /* Devuelve el ID numérico del usuario actual */

    /* Consultar base de datos del sistema /etc/passwd */
    struct passwd *pw = getpwuid(uid);
    const char *username = pw ? pw->pw_name : "usuario desconocido";

    printf(COLOR_RESULT "¡Hola, %s! Bienvenido al Shell de Aprendizaje de Syscalls.\n" COLOR_RESET, username);
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: hora
 * ====================================================================================
 * Obtiene y muestra la hora actual en la máquina.
 * 
 * Syscalls explicadas:
 * 1. time(2): Devuelve el número de segundos transcurridos desde el Unix Epoch (1 de enero de 1970).
 * 
 * Funciones de biblioteca explicadas:
 * - localtime(3): Traduce los segundos epoch brutos en una estructura estructurada struct tm 
 *   que separa horas, minutos, segundos, etc., ajustado a la zona horaria local.
 * - strftime(3): Formatea la estructura de tiempo struct tm en un string de texto (HH:MM:SS).
 */
int cmd_hora(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 1. LLAMADA AL SISTEMA: time */
    LOG_SYSCALL("time", "NULL");
    time_t rawtime = time(NULL);
    if (rawtime == -1) {
        LOG_SYSCALL_ERROR("error");
        return 1;
    }
    LOG_SYSCALL_RESULT(rawtime); /* Retorna el timestamp Epoch Unix */

    /* Formateo de los segundos a hora legible */
    struct tm *timeinfo = localtime(&rawtime);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);

    printf(COLOR_RESULT "Hora actual: %s\n" COLOR_RESET, buffer);
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: fecha
 * ====================================================================================
 * Obtiene y muestra la fecha del calendario actual.
 * 
 * Similar al comando de la hora, utiliza la llamada time(2) para obtener la estampa de
 * tiempo y strftime(3) para formatear en formato YYYY-MM-DD.
 */
int cmd_fecha(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 1. LLAMADA AL SISTEMA: time */
    LOG_SYSCALL("time", "NULL");
    time_t rawtime = time(NULL);
    if (rawtime == -1) {
        LOG_SYSCALL_ERROR("error");
        return 1;
    }
    LOG_SYSCALL_RESULT(rawtime);

    /* Formateo de los segundos a fecha legible */
    struct tm *timeinfo = localtime(&rawtime);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", timeinfo);

    printf(COLOR_RESULT "Fecha actual: %s\n" COLOR_RESET, buffer);
    return 0;
}


//==============================
/**
 * ====================================================================================
 * COMANDO: despedir
 * ====================================================================================
 * Saluda al usuario autenticado en el sistema.
 * 
 * Syscalls explicadas:
 * 1. getuid(2): Obtiene el Identificador de Usuario (UID) real del proceso.
 *    - Retorna el entero que representa la cuenta (e.g. 0 para root, 1000 para usuarios).
 * 
 * Funciones de biblioteca explicadas:
 * - getpwuid(3): Lee la base de datos de usuarios (usualmente el archivo del sistema `/etc/passwd`)
 *   para traducir el UID numérico a una estructura struct passwd con el nombre de usuario de login.
 */
int cmd_despedir(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 1. LLAMADA AL SISTEMA: getuid */
    LOG_SYSCALL("getuid", "");
    uid_t uid = getuid();
    LOG_SYSCALL_RESULT(uid); /* Devuelve el ID numérico del usuario actual */

    /* Consultar base de datos del sistema /etc/passwd */
    struct passwd *pw = getpwuid(uid);
    const char *username = pw ? pw->pw_name : "usuario desconocido";

    printf(COLOR_RESULT "¡Adios, %s! Qué Dios de bendiga, Camina por la sombra.\n" COLOR_RESET, username);
    return 0;
}