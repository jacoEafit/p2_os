#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * ====================================================================================
 * Códigos de Escape ANSI para Colores en Terminal
 * ====================================================================================
 * Permiten imprimir texto en la consola de comandos usando colores y formatos.
 * Cada macro representa una secuencia de escape que el emulador de terminal interpreta
 * para cambiar el color del lápiz de dibujo. COLOR_RESET devuelve el color a la normalidad.
 */
#define COLOR_RESET     "\033[0m"      /* Restablece todos los atributos de color y estilo */
#define COLOR_PROMPT    "\033[1;36m"   /* Cian brillante: Usado para el prompt interactivo 'sys-shell>' */
#define COLOR_SYSCALL   "\033[1;35m"   /* Púrpura/Magenta brillante: Resalta el nombre de las syscalls ejecutadas */
#define COLOR_PARAM     "\033[0;33m"   /* Amarillo/Marrón: Usado para valores numéricos o parámetros importantes */
#define COLOR_RESULT    "\033[1;32m"   /* Verde brillante: Destaca valores de retorno exitosos (>= 0 o punteros válidos) */
#define COLOR_ERROR     "\033[1;31m"   /* Rojo brillante: Resalta fallos y descripciones de error (errno) */
#define COLOR_INFO      "\033[0;90m"   /* Gris oscuro: Texto de depuración o comentarios explicativos menores */
#define COLOR_TITLE     "\033[1;97m"   /* Blanco brillante en negrita: Para encabezados de tablas y banners */
#define COLOR_CATEGORY  "\033[1;34m"   /* Azul brillante: Categorías del sistema en el comando 'help' */

/**
 * ====================================================================================
 * Macros Educativas de Trazado de Llamadas al Sistema (Syscalls)
 * ====================================================================================
 * Estas macros imitan el comportamiento de la herramienta 'strace' de Linux.
 * Al ejecutarse una syscall en el código, se llama a LOG_SYSCALL con el nombre
 * de la función y sus argumentos para mostrarlos formateados.
 * Dependiendo del resultado, se invocará LOG_SYSCALL_RESULT o LOG_SYSCALL_ERROR.
 */

/* Imprime la llamada al sistema y sus parámetros */
#define LOG_SYSCALL(syscall_name, format, ...) \
    printf(COLOR_SYSCALL "[syscall] " syscall_name COLOR_RESET "(" format ") ... " COLOR_RESET, ##__VA_ARGS__)

/* Imprime el valor de retorno en verde cuando es un número entero exitoso (e.g., File Descriptor o bytes) */
#define LOG_SYSCALL_RESULT(result) \
    printf("= " COLOR_RESULT "%ld" COLOR_RESET "\n", (long)(result))

/* Imprime la dirección de memoria de retorno en verde cuando se retorna un puntero (e.g., mmap, sbrk) */
#define LOG_SYSCALL_RESULT_PTR(result) \
    printf("= " COLOR_RESULT "%p" COLOR_RESET "\n", (void*)(result))

/* Imprime el valor -1 en rojo indicando un error y adjunta el texto descriptivo del código errno */
#define LOG_SYSCALL_ERROR(err_name) \
    printf("= " COLOR_ERROR "-1 (%s)" COLOR_RESET "\n", err_name)


typedef struct {
    int fd;
} Editor;

typedef struct {
    const char *name;
    const char *category;
    const char *usage;
    const char *description;
    const char *syscalls;
    int (*handler)(int argc, char **argv, Editor *editor);
} EditorCommand;


/**
 * ====================================================================================
 * Estructura de Registro de Comandos
 * ====================================================================================
 * Define un comando del shell con toda su metadata con fines pedagógicos.
 * Permite listar automáticamente los comandos asociados a syscalls de forma dinámica.
 */

typedef struct {
    const char *name;        /* Nombre textual del comando que el usuario escribe (e.g., 'd_create') */
    const char *category;    /* Categoría de ordenación: "datos", "memoria", "monitoreo", "utilidades" */
    const char *usage;       /* Sintaxis de uso del comando para mostrar en caso de error */
    const char *description; /* Explicación en español de lo que hace el comando a nivel lógico */
    const char *syscalls;    /* Explicación de las syscalls involucradas que se mostrarán en la ayuda */
    int (*handler)(int argc, char **argv); /* Puntero a la función que implementa la lógica del comando */
} Command;

/* ====================================================================================
 * Declaración de Prototipos de Comandos
 * ====================================================================================
 */

/* --- Categoría: Datos y Archivos (cat_datos.c) --- */
int cmd_d_create(int argc, char **argv); /* Syscalls: open, write, close */
int cmd_d_read(int argc, char **argv);   /* Syscalls: open, read, close */
int cmd_d_info(int argc, char **argv);   /* Syscalls: stat */
int cmd_d_copy(int argc, char **argv);   /* Syscalls: open, read, write, close */

/* --- Categoría: Memoria (cat_memoria.c) --- */
int cmd_m_sbrk(int argc, char **argv);   /* Syscalls: sbrk / brk */
int cmd_m_mmap(int argc, char **argv);   /* Syscalls: mmap, munmap */
int cmd_m_info(int argc, char **argv);   /* Mecanismo: Lectura de /proc/self/status */

/* --- Categoría: Monitoreo/Procesos (cat_monitoreo.c) --- */
int cmd_p_fork(int argc, char **argv);    /* Syscalls: fork, getpid, getppid, waitpid */
int cmd_p_exec(int argc, char **argv);    /* Syscalls: fork, execvp, waitpid */
int cmd_p_kill(int argc, char **argv);    /* Syscalls: kill */
int cmd_p_monitor(int argc, char **argv); /* Syscalls: getrusage */

/* --- Categoría: Utilidades (cat_util.c) --- */
int cmd_saludar(int argc, char **argv);   /* Syscalls: getuid */
int cmd_despedir(int argc, char **argv);  /* Syscalls: getuid */
int cmd_hora(int argc, char **argv);      /* Syscalls: time */
int cmd_fecha(int argc, char **argv);     /* Syscalls: time */

/* --- Categoría: Compresor (cat_compresor.c) --- */
int cmd_c_comprimir(int argc, char **argv);    /* Syscalls: open, fstat, pread, write, close, pthread_create, pthread_join */
int cmd_c_descomprimir(int argc, char **argv); /* Syscalls: open, read, write, close, pthread_create, pthread_join */
int cmd_c_estado(int argc, char **argv);       /* Sincronización: pthread_mutex_lock/unlock; Syscalls: clock_gettime */
int cmd_c_cancelar(int argc, char **argv);     /* Syscalls: pthread_cond_broadcast, pthread_join, unlink */
int cmd_c_verificar(int argc, char **argv);    /* Syscalls: open, read, close */
int cmd_c_config(int argc, char **argv);       /* Sincronización: pthread_mutex_lock/unlock */
void compresor_finalizar(void);               /* Espera (join) y libera el trabajo en curso al salir */

int cmd_o(int argc, char **argv, Editor *editor);
int cmd_p(int argc, char **argv, Editor *editor);
int cmd_a(int argc, char **argv, Editor *editor);
int cmd_d(int argc, char **argv, Editor *editor);
int cmd_q(int argc, char **argv, Editor *editor);
int cmd_i(int argc, char **argv, Editor *editor);
int cmd_s(int argc, char **argv, Editor *editor);


#endif /* SHELL_H */
