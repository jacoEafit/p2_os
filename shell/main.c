#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 64
#define MAX_LINE 2048


EditorCommand editorCommands[] = {
    {
        "o",
        "editor",
        "o [archivo]",
        "Abre un archivo",
        "open(), close()",
        cmd_o
    },
    {
        "p",
        "editor",
        "p [n]",
        "Imprime la linea n, sin el n imprime todo el archivo",
        "lseek(), read(), write()",
        cmd_p
    },
    {
        "a",
        "editor",
        "a [texto]",
        "Añade texto al final",
        "lseek(), read(), write()",
        cmd_a
    },
    {
        "d",
        "editor",
        "d [n]",
        "Borra la linea n",
        "lseek(), read(), write(), ftruncate()",
        cmd_d
    },
    {
        "q",
        "editor",
        "q",
        "Cierra el file descriptor",
        "close()",
        cmd_q
    },
    {
        "i",
        "editor",
        "i [n] [texto]",
        "Inserta el texto en la linea n",
        "lseek(), read(), write()",
        cmd_i
    },
    {
        "s",
        "editor",
        "s [palabra]",
        "Busca la palabra",
        "lseek(), read()",
        cmd_s
    }
};
/**
 * ====================================================================================
 * TABLA GLOBAL DE REGISTRO DE COMANDOS
 * ====================================================================================
 * Almacena los punteros a funciones e información descriptiva de cada comando educativo.
 * Cada elemento de la tabla inicializa una estructura Command declarada en shell.h.
 */

Command commands[] = {
    /* --- Categoría: Datos --- */
    {
        "d_create", "datos", 
        "d_create <archivo> \"<texto>\"", 
        "Crea un archivo escribiendo un texto en él.",
        "open(2), write(2), close(2)", 
        cmd_d_create
    },
    {
        "d_read", "datos", 
        "d_read <archivo>", 
        "Lee y muestra el contenido de un archivo.",
        "open(2), read(2), close(2)", 
        cmd_d_read
    },
    {
        "d_info", "datos", 
        "d_info <archivo>", 
        "Muestra metadatos detallados de un archivo.",
        "stat(2)", 
        cmd_d_info
    },
    {
        "d_copy", "datos", 
        "d_copy <origen> <destino>", 
        "Copia recursiva o lineal de bytes entre archivos.",
        "open(2), read(2), write(2), close(2)", 
        cmd_d_copy
    },

    /* --- Categoría: Memoria --- */
    {
        "m_sbrk", "memoria", 
        "m_sbrk <incremento_bytes>", 
        "Modifica el program break de la sección heap.",
        "sbrk(2) / brk(2)", 
        cmd_m_sbrk
    },
    {
        "m_mmap", "memoria", 
        "m_mmap <tamaño_bytes>", 
        "Mapea una zona de memoria anónima y escribe un patrón.",
        "mmap(2), munmap(2)", 
        cmd_m_mmap
    },
    {
        "m_info", "memoria", 
        "m_info", 
        "Muestra el estado del mapa de memoria del proceso actual.",
        "Lectura directa de /proc/self/status", 
        cmd_m_info
    },

    /* --- Categoría: Monitoreo/Procesos --- */
    {
        "p_fork", "monitoreo", 
        "p_fork", 
        "Crea un proceso hijo, demuestra sincronización y códigos de salida.",
        "fork(2), getpid(2), getppid(2), waitpid(2)", 
        cmd_p_fork
    },
    {
        "p_exec", "monitoreo", 
        "p_exec <comando> [argumentos...]", 
        "Crea un proceso hijo y ejecuta un comando externo del sistema.",
        "fork(2), execvp(3), waitpid(2)", 
        cmd_p_exec
    },
    {
        "p_kill", "monitoreo", 
        "p_kill <pid> <numero_señal>", 
        "Envía una señal específica a un proceso en ejecución.",
        "kill(2)", 
        cmd_p_kill
    },
    {
        "p_monitor", "monitoreo", 
        "p_monitor", 
        "Muestra el uso detallado de recursos de la CPU y memoria del shell.",
        "getrusage(2)", 
        cmd_p_monitor
    },

    /* --- Categoría: Utilidades --- */
    {
        "saludar", "utilidades",
        "saludar",
        "Muestra un saludo personalizado para el usuario actual.",
        "getuid(2)",
        cmd_saludar
    },
    {
        "despedir", "utilidades",
        "despedir",
        "Muestra un mensaje de despedida personalizado para el usuario actual.",
        "getuid(2)",
        cmd_despedir
    },
    {
        "hora", "utilidades",
        "hora",
        "Muestra la hora actual del sistema.",
        "time(2)",
        cmd_hora
    },
    {
        "fecha", "utilidades",
        "fecha",
        "Muestra la fecha actual del sistema.",
        "time(2)",
        cmd_fecha
    }
};

/* Número total de comandos en el shell */
const int num_commands = sizeof(commands) / sizeof(commands[0]);
const int num_editor_commands = sizeof(editorCommands) / sizeof(editorCommands[0]);

/**
 * ====================================================================================
 * ANALIZADOR DE LÍNEA DE COMANDOS (TOKENIZADOR)
 * ====================================================================================
 * Esta función toma la línea de entrada introducida por el usuario y la divide en 
 * argumentos individuales. Soporta comillas dobles (") para permitir argumentos
 * que contienen espacios en blanco, como en: d_create archivo.txt "Este es el texto"
 *
 * Parámetros:
 * - line: La línea cruda leída del teclado. Modificada in-situ colocando caracteres nulos (\0).
 * - argv: Array de punteros que se llenará apuntando al inicio de cada argumento.
 *
 * Retorna:
 * - El número de argumentos (argc) detectados.
 */
int parse_line(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    int in_quote = 0;
    char *arg_start = NULL;

    while (*p) {
        if (!in_quote && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            /* Espacio en blanco fuera de comillas: Terminar el token actual */
            if (arg_start != NULL) {
                *p = '\0'; /* Inserta carácter de fin de cadena */
                argv[argc++] = arg_start;
                arg_start = NULL;
            }
        } else if (*p == '"') {
            /* Cambiar el estado de lectura de comillas */
            if (in_quote) {
                /* Fin del bloque entre comillas */
                *p = '\0';
                argv[argc++] = arg_start;
                arg_start = NULL;
                in_quote = 0;
            } else {
                /* Inicio del bloque entre comillas */
                in_quote = 1;
                arg_start = p + 1;
            }
        } else {
            /* Carácter ordinario del argumento */
            if (arg_start == NULL) {
                arg_start = p;
            }
        }
        p++;
    }
    /* Añadir el último argumento si quedó pendiente */
    if (arg_start != NULL) {
        argv[argc++] = arg_start;
    }
    argv[argc] = NULL; /* Convención POSIX de terminar argv con NULL */
    return argc;
}

/**
 * ====================================================================================
 * FUNCIÓN DE AYUDA INTERACTIVA (HELP)
 * ====================================================================================
 * Muestra información pedagógica general del shell, por categoría o por comando individual.
 */
void print_help(const char *arg) {
    if (arg == NULL) {
        /* Caso 1: Escribió 'help' solo: Mostrar categorías principales */
        printf(COLOR_TITLE "\n--- Shell de Aprendizaje de Syscalls (SO2026B) ---\n" COLOR_RESET);
        printf("Este shell te permite explorar cómo funcionan las llamadas al sistema en Linux.\n");
        printf("Los comandos están clasificados en categorías.\n\n");
        printf("Categorías disponibles:\n");
        printf("  " COLOR_CATEGORY "datos" COLOR_RESET "      - Comandos de archivos y datos (open, read, write, stat, ...)\n");
        printf("  " COLOR_CATEGORY "memoria" COLOR_RESET "    - Comandos de control de heap y memoria (sbrk, mmap, ...)\n");
        printf("  " COLOR_CATEGORY "monitoreo" COLOR_RESET "  - Comandos de procesos, señales y recursos (fork, exec, kill, getrusage)\n");
        printf("  " COLOR_CATEGORY "utilidades" COLOR_RESET " - Comandos útiles del sistema (saludar, hora, fecha, despedir)\n");
        printf("  " COLOR_CATEGORY "editor" COLOR_RESET " - Comandos de edición de archivos (abrir, imprimir, añadir, borrar, etc)\n\n");
        printf("Uso general:\n");
        printf("  " COLOR_PROMPT "help <categoria>" COLOR_RESET "  - Muestra comandos específicos de una categoría.\n");
        printf("  " COLOR_PROMPT "help <comando>" COLOR_RESET "    - Explica el uso y las syscalls de un comando específico.\n");
        printf("  " COLOR_PROMPT "clear" COLOR_RESET "             - Limpia la pantalla.\n");
        printf("  " COLOR_PROMPT "exit" COLOR_RESET "              - Cierra el shell.\n\n");
        return;
    }

    /* Caso 2: El usuario escribió 'help <categoria>': Mostrar comandos del grupo */
    if (strcmp(arg, "datos") == 0 || strcmp(arg, "memoria") == 0 || 
        strcmp(arg, "monitoreo") == 0 || strcmp(arg, "utilidades") == 0) {
        printf(COLOR_TITLE "\n--- Categoría: %s ---\n" COLOR_RESET, arg);
        for (int i = 0; i < num_commands; i++) {
            if (strcmp(commands[i].category, arg) == 0) {
                printf("  " COLOR_PROMPT "%-10s" COLOR_RESET " -> %s\n", commands[i].name, commands[i].description);
                printf("                " COLOR_INFO "Llamada(s): %s" COLOR_RESET "\n\n", commands[i].syscalls);
            }
        }
        return;
    }

    /* Caso 2: El usuario escribió 'help <categoria>': Mostrar comandos del grupo editor*/
    if (strcmp(arg, "editor") == 0){
        printf(COLOR_TITLE "\n--- Categoría: editor de texto ---\n" COLOR_RESET);
        for (int i = 0; i < num_editor_commands; i++) {
            if (strcmp(editorCommands[i].category, arg) == 0) {
                printf("  " COLOR_PROMPT "%-10s" COLOR_RESET " -> %s\n", editorCommands[i].name, editorCommands[i].description);
                printf("                " COLOR_INFO "Llamada(s): %s" COLOR_RESET "\n\n", editorCommands[i].syscalls);
            }
        }
        return;
    }

    /* Caso 3: El usuario escribió 'help <comando>': Explicar syscalls individuales */
    for (int i = 0; i < num_commands; i++) {
        if (strcmp(commands[i].name, arg) == 0) {
            printf(COLOR_TITLE "\nDetalles de comando: %s\n" COLOR_RESET, commands[i].name);
            printf("  Descripción:  %s\n", commands[i].description);
            printf("  Uso:          " COLOR_PARAM "%s" COLOR_RESET "\n", commands[i].usage);
            printf("  Syscalls:     " COLOR_SYSCALL "%s" COLOR_RESET "\n\n", commands[i].syscalls);
            return;
        }
    }
    
    /* Caso 3: El usuario escribió 'help <editor>': Explicar syscalls individuales del editor de texto */
    for (int i = 0; i < num_editor_commands; i++) {
        if (strcmp(editorCommands[i].name, arg) == 0) {
            printf(COLOR_TITLE "\nDetalles de comando: %s\n" COLOR_RESET, editorCommands[i].name);
            printf("  Descripción:  %s\n", editorCommands[i].description);
            printf("  Uso:          " COLOR_PARAM "%s" COLOR_RESET "\n", editorCommands[i].usage);
            printf("  Syscalls:     " COLOR_SYSCALL "%s" COLOR_RESET "\n\n", editorCommands[i].syscalls);
            return;
        }
    }

    printf(COLOR_ERROR "Categoría o comando '%s' no reconocido. Escribe 'help' para ver la ayuda.\n" COLOR_RESET, arg);
}

/**
 * ====================================================================================
 * BUCLE REPL PRINCIPAL (Read-Eval-Print Loop)
 * ====================================================================================
 * Controla el ciclo de vida del shell:
 * 1. Read: Lee la entrada del usuario usando fgets().
 * 2. Eval: Parse y busca si el comando coincide con built-ins o funciones registradas.
 * 3. Print: Imprime los resultados y las llamadas al sistema en consola.
 * 4. Loop: Repite el ciclo infinitamente hasta escribir 'exit' o presionar Ctrl+D.
 */
int main() {
    char line[MAX_LINE];
    char *argv[MAX_ARGS];

    Editor editor;
    editor.fd = -1;

    /* Banner de bienvenida premium */
    printf(COLOR_TITLE "========================================================\n" COLOR_RESET);
    printf(COLOR_TITLE "   Shell Educativo (Llamadas al Sistema de Linux) (EAFITOS)\n" COLOR_RESET);
    printf(COLOR_INFO "    Asignatura: SO2026B0 (Sistemas Operativos)\n" COLOR_RESET);
    printf(COLOR_INFO "    Escribe 'help' para iniciar. Desarrollado en C.\n" COLOR_RESET);
    printf(COLOR_TITLE "========================================================\n\n" COLOR_RESET);

    while (1) {
        /* Imprimir prompt cian interactivo */
        printf(COLOR_PROMPT "eafitOS> " COLOR_RESET);
        fflush(stdout); /* Asegurar que se muestre en pantalla antes de bloquear en fgets */

        /* Leer línea de entrada. Retorna NULL en EOF (Ctrl+D) */
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        /* Tokenizar línea leída */
        int argc = parse_line(line, argv);
        if (argc == 0) {
            continue; /* Ignorar comandos vacíos */
        }

        /* Comandos Built-in generales */
        if (strcmp(argv[0], "exit") == 0) {
            printf(COLOR_INFO "Saliendo del shell educativo. ¡Hasta luego!\n" COLOR_RESET);
            break;
        } else if (strcmp(argv[0], "clear") == 0) {
            printf("\033[H\033[J"); /* Limpia la pantalla usando secuencias de escape ANSI */
            continue;
        } else if (strcmp(argv[0], "help") == 0) {
            if (argc > 1) {
                print_help(argv[1]);
            } else {
                print_help(NULL);
            }
            continue;
        }

        /* Enrutar la ejecución buscando en la lista de comandos registrados */
        int found = 0;
        for (int i = 0; i < num_commands; i++) {
            if (strcmp(argv[0], commands[i].name) == 0) {
                commands[i].handler(argc, argv);
                found = 1;
                break;
            }
        }
        
        // Buscar en los comandos del editor
        if (!found)
        {
            for (int i = 0; i < num_editor_commands; i++) {
                if (strcmp(argv[0], editorCommands[i].name) == 0) {
                    editorCommands[i].handler(argc, argv, &editor);
                    found = 1;
                    break;
                }
            }
        }

        /* Si no se encuentra en el registro educativo del shell */
        if (!found) {
            printf(COLOR_ERROR "Comando '%s' no encontrado en el shell educativo.\n" COLOR_RESET, argv[0]);
            printf(COLOR_INFO "Prueba usando 'p_exec %s' si quieres ejecutarlo como un binario de Linux externo, o escribe 'help'.\n" COLOR_RESET, argv[0]);
        }
        printf("\n");
    }

    return 0;
}
