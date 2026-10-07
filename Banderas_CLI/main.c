#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

/* 
 * ====================================================================================
 * Códigos de Color ANSI para mejorar la estética en la terminal (Aesthetic Terminal)
 * ====================================================================================
 * Permiten dar un formato de salida premium en la terminal de Linux.
 * Cada macro representa una secuencia de escape que cambia el color de impresión.
 */
#define COLOR_RESET   "\033[0m"      /* Restablece todos los colores a la configuración de la terminal */
#define COLOR_TITLE   "\033[1;97m"   /* Blanco brillante en negrita: Para encabezados importantes */
#define COLOR_PARAM   "\033[1;36m"   /* Cian brillante: Para nombres de banderas (ej: -n, --port) */
#define COLOR_VAL     "\033[1;32m"   /* Verde brillante: Para valores asignados a las variables */
#define COLOR_ERROR   "\033[1;31m"   /* Rojo brillante: Para indicar errores de formato o valores ausentes */
#define COLOR_INFO    "\033[0;90m"   /* Gris oscuro: Para trazas educativas y depuración del sistema */
#define COLOR_BANNER  "\033[1;35m"   /* Magenta brillante: Para separar los modos de ejecución del parseador */

/**
 * ====================================================================================
 * ESTRUCTURA DE CONFIGURACIÓN DEL SISTEMA
 * ====================================================================================
 * Almacena los valores leídos desde la línea de comandos. 
 * Se pasa por referencia a los diferentes parseadores para poblar sus campos.
 */
typedef struct {
    char *name;   /* Nombre de usuario leido de la bandera (string) */
    int port;     /* Puerto de red (número entero) */
    int verbose;  /* Bandera lógica (0 = Falso, 1 = Verdadero) para activar depuración */
} CliConfig;

/**
 * Muestra las instrucciones de uso en caso de error o cuando el usuario solicita ayuda.
 * Explica detalladamente los tres modos didácticos implementados.
 */
void print_usage(const char *prog_name) {
    printf(COLOR_TITLE "Uso del programa:\n" COLOR_RESET);
    printf("  %s <modo> [banderas...]\n\n", prog_name);
    printf(COLOR_TITLE "Modos disponibles (primer argumento):\n" COLOR_RESET);
    printf("  " COLOR_PARAM "manual" COLOR_RESET "      - Parseo manual directo iterando sobre el arreglo argv\n");
    printf("  " COLOR_PARAM "getopt" COLOR_RESET "      - Parseo estándar POSIX usando getopt()\n");
    printf("  " COLOR_PARAM "getopt_long" COLOR_RESET " - Parseo estándar GNU usando getopt_long()\n\n");
    printf(COLOR_TITLE "Banderas soportadas:\n" COLOR_RESET);
    printf("  " COLOR_PARAM "-n, --name <texto>" COLOR_RESET "  Nombre de usuario (ej: -n Edison)\n");
    printf("  " COLOR_PARAM "-p, --port <entero>" COLOR_RESET " Puerto de conexión (ej: -p 8080)\n");
    printf("  " COLOR_PARAM "-v, --verbose" COLOR_RESET "       Activa logs de depuración adicionales\n");
    printf("  " COLOR_PARAM "-h, --help" COLOR_RESET "          Muestra esta ayuda de uso\n");
}

/**
 * Muestra de manera didáctica cómo se almacena el vector 'argv' en el stack
 * de la memoria virtual del proceso al iniciarse.
 * Muestra el número de argumentos (argc), el contenido de cada string de argv
 * y su dirección física en el stack.
 */
void print_argv_structure(int argc, char **argv) {
    printf(COLOR_VAL "[Trazado] Estructura recibida en el stack de memoria (argv):\n" COLOR_RESET);
    printf(COLOR_INFO "  argc = %d (número de argumentos pasados)\n" COLOR_RESET, argc);
    for (int i = 0; i < argc; i++) {
        /* Imprime el índice, el valor textual y la dirección de memoria virtual apuntada */
        printf(COLOR_VAL"  argv[%d] = \"%s\" (Ubicación: %p)\n" COLOR_RESET, i, argv[i], (void*)argv[i]);
    }
    printf("\n");
}

/**
 * Muestra los resultados almacenados en la estructura CliConfig para validar
 * que el parseo de banderas haya sido exitoso.
 */
void print_config_result(const CliConfig *config) {
    printf(COLOR_TITLE "========================================\n" COLOR_RESET);
    printf(COLOR_TITLE "    Configuración Final Obtenida\n" COLOR_RESET);
    printf(COLOR_TITLE "========================================\n" COLOR_RESET);
    printf("  Nombre (Name):   " COLOR_VAL "%s" COLOR_RESET "\n", config->name ? config->name : "(no definido)");
    printf("  Puerto (Port):   " COLOR_VAL "%d" COLOR_RESET "\n", config->port);
    printf("  Modo Detallado:  " COLOR_VAL "%s" COLOR_RESET "\n", config->verbose ? "ACTIVO (True)" : "INACTIVO (False)");
    printf(COLOR_TITLE "========================================\n\n" COLOR_RESET);
}
            
/**
 * ====================================================================================
 * ENFOQUE 1: PARSEO MANUAL DE ARGUMENTOS
 * ====================================================================================
 * Lógica:
 * Recorremos manualmente el arreglo argv desde i = 2 usando un bucle.
 * Comparamos cadenas de forma directa usando strcmp().
 * Si una bandera espera un argumento (como -n o -p), verificamos que no cause un
 * acceso fuera de los límites de memoria validando (i + 1 < argc) y avanzamos el
 * puntero del bucle (i++) para consumir el argumento.
 */
void parse_manual(int argc, char **argv, CliConfig *config) {
    printf(COLOR_BANNER "--- Iniciando Parseo Manual (Iteración Directa sobre argv) ---\n" COLOR_RESET);
    printf(COLOR_INFO "Recorriendo argv desde el índice 2 (saltando programa y modo)...\n" COLOR_RESET);

    for (int i = 2; i < argc; i++) {
        printf(COLOR_INFO "[manual-step] Evaluando argv[%d] = \"%s\"\n" COLOR_RESET, i, argv[i]);

        /* Caso 1: Bandera de Nombre (-n o --name) */
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) {
            /* IMPORTANTE: Evitar lectura fuera de límites.
             * Si el usuario escribe '-n' al final del comando sin un valor posterior, i+1 sería igual a argc. */
            if (i + 1 < argc) {
                config->name = argv[i + 1]; /* Asigna el puntero directamente del arreglo argv */
                printf(COLOR_INFO "  -> Detectado: Name = \"%s\"\n" COLOR_RESET, config->name);
                i++; /* Incrementamos el iterador para saltar el argumento de valor ya consumido */
            } else {
                fprintf(stderr, COLOR_ERROR "Error: La bandera '%s' requiere un argumento de texto.\n" COLOR_RESET, argv[i]);
                exit(1);
            }
        } 
        /* Caso 2: Bandera de Puerto (-p o --port) */
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                /* atoi convierte la cadena de caracteres de texto en un número entero */
                config->port = atoi(argv[i + 1]);
                printf(COLOR_INFO "  -> Detectado: Port = %d\n" COLOR_RESET, config->port);
                i++; /* Saltar el valor consumido */
            } else {
                fprintf(stderr, COLOR_ERROR "Error: La bandera '%s' requiere un argumento numérico.\n" COLOR_RESET, argv[i]);
                exit(1);
            }
        } 
        /* Caso 3: Bandera booleana de depuración (-v o --verbose) */
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            config->verbose = 1; /* Asignar 1 (Verdadero) */
            printf(COLOR_INFO "  -> Detectado: Modo Verbose activado\n" COLOR_RESET);
        } 
        /* Caso 4: Solicitud de ayuda (-h o --help) */
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } 
        /* Caso 5: Argumento no reconocido (o bandera inválida) */
        else {
            fprintf(stderr, COLOR_ERROR "Advertencia: Bandera '%s' no reconocida. Ignorada.\n" COLOR_RESET, argv[i]);
        }
    }
}

/**
 * ====================================================================================
 * ENFOQUE 2: POSIX getopt()
 * ====================================================================================
 * Lógica:
 * getopt(3) procesa secuencialmente argumentos con guión (-).
 * Modifica variables del sistema para llevar la contabilidad:
 * - optind: Índice en argv del siguiente elemento a analizar.
 * - optarg: Guarda el puntero al valor asociado a una bandera (si corresponde).
 * - optopt: Guarda la bandera inválida si getopt() falla.
 * 
 * Cadena de formato de getopt:
 * "n:p:vh"
 * - Las letras simples ('v', 'h') representan opciones booleanas simples.
 * - Las letras seguidas por dos puntos (':') indican que la bandera requiere obligatoriamente
 *   un argumento de valor adjunto (ej: 'n:', 'p:').
 */
void parse_getopt(int argc, char **argv, CliConfig *config) {
    printf(COLOR_BANNER "--- Iniciando Parseo POSIX usando getopt() ---\n" COLOR_RESET);

    int opt;

    /* REINICIO DE getopt:
     * Por defecto, getopt empieza a leer en optind = 1. Como argv[1] es nuestro "modo"
     * de parseo ("getopt"), debemos decirle a la biblioteca que empiece a parsear en optind = 2.
     */
    optind = 2;

    /* getopt() retorna el caracter de la bandera encontrada, o -1 al finalizar */
    while ((opt = getopt(argc, argv, "n:p:vh")) != -1) {
        printf(COLOR_INFO "[getopt-step] Encontrado carácter de opción '-%c' (optind=%d)\n" COLOR_RESET, opt, optind);

        switch (opt) {
            case 'n':
                /* optarg apunta automáticamente al valor posterior de la bandera */
                config->name = optarg;
                printf(COLOR_INFO "  -> optarg = \"%s\"\n" COLOR_RESET, optarg);
                break;
            case 'p':
                config->port = atoi(optarg);
                printf(COLOR_INFO "  -> optarg = \"%s\" (entero convertido: %d)\n" COLOR_RESET, optarg, config->port);
                break;
            case 'v':
                config->verbose = 1;
                printf(COLOR_INFO "  -> Verbose activado\n" COLOR_RESET);
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case '?':
                /* Si getopt() encuentra una opción no registrada o si le falta un valor,
                 * retorna '?' e imprime un error predeterminado a stderr.
                 * El caracter fallido se almacena en la variable global 'optopt'. */
                fprintf(stderr, COLOR_ERROR "Error: Formato inválido o falta argumento para opción '-%c'\n" COLOR_RESET, optopt);
                exit(1);
            default:
                break;
        }
    }
}

/**
 * ====================================================================================
 * ENFOQUE 3: GNU getopt_long()
 * ====================================================================================
 * Lógica:
 * Permite procesar argumentos largos con doble guión (--name, --port).
 * Requiere un arreglo de estructuras `struct option` que definen las opciones válidas:
 * 
 * struct option {
 *   const char *name;    // Nombre de la opción larga sin guiones (ej. "port")
 *   int         has_arg; // no_argument (0), required_argument (1), optional_argument (2)
 *   int        *flag;    // Si es NULL, getopt_long retorna el carácter de 'val'
 *   int         val;     // El carácter equivalente de opción corta (ej: 'p')
 * };
 */
void parse_getopt_long(int argc, char **argv, CliConfig *config) {
    printf(COLOR_BANNER "--- Iniciando Parseo GNU usando getopt_long() ---\n" COLOR_RESET);

    int opt;
    int option_index = 0; /* Almacena el índice de la opción larga encontrada en el arreglo */

    /* Definición de la tabla de mapeo de opciones largas a sus equivalencias cortas */
    static struct option long_options[] = {
        {"name",    required_argument, NULL, 'n'}, /* --name equivale a -n */
        {"port",    required_argument, NULL, 'p'}, /* --port equivale a -p */
        {"verbose", no_argument,       NULL, 'v'}, /* --verbose equivale a -v */
        {"help",    no_argument,       NULL, 'h'}, /* --help equivale a -h */
        {NULL,      0,                 NULL,  0 }  /* Sentinela obligatorio de cierre con valores nulos */
    };

    /* Reiniciamos el cursor al índice 2 para evitar analizar el comando de modo (argv[1]) */
    optind = 2;

    /* getopt_long lee tanto las opciones cortas de la cadena format "n:p:vh"
     * como las opciones largas definidas en el arreglo 'long_options' */
    while ((opt = getopt_long(argc, argv, "n:p:vh", long_options, &option_index)) != -1) {
        printf(COLOR_INFO "[getopt-long-step] Opción corta: '%c' (Larga: --%s, optind=%d)\n" COLOR_RESET, 
               opt, long_options[option_index].name, optind);

        switch (opt) {
            case 'n':
                config->name = optarg;
                printf(COLOR_INFO "  -> optarg = \"%s\"\n" COLOR_RESET, optarg);
                break;
            case 'p':
                config->port = atoi(optarg);
                printf(COLOR_INFO "  -> optarg = \"%s\" (entero: %d)\n" COLOR_RESET, optarg, config->port);
                break;
            case 'v':
                config->verbose = 1;
                printf(COLOR_INFO "  -> Verbose activado\n" COLOR_RESET);
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case '?':
                /* Error ya reportado a stderr por la biblioteca automáticamente */
                exit(1);
            default:
                break;
        }
    }
}

int main(int argc, char **argv) {
    /* Configuración por defecto antes de leer la línea de comandos */
    CliConfig config = {
        .name = NULL,
        .port = 80,      /* Puerto predeterminado HTTP */
        .verbose = 0     /* Depuración desactivada por defecto */
    };

    /* Validar entrada mínima: debe tener al menos el modo a probar (manual, getopt...) */
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Mostrar visualmente el arreglo argv de la pila */
    print_argv_structure(argc, argv);

    /* Capturar el primer argumento que define el modo del analizador */
    const char *mode = argv[1];

    if (strcmp(mode, "manual") == 0) {
        parse_manual(argc, argv, &config);
    } 
    else if (strcmp(mode, "getopt") == 0) {
        parse_getopt(argc, argv, &config);
    } 
    else if (strcmp(mode, "getopt_long") == 0) {
        parse_getopt_long(argc, argv, &config);
    } 
    else if (strcmp(mode, "-h") == 0 || strcmp(mode, "--help") == 0 || strcmp(mode, "help") == 0) {
        print_usage(argv[0]);
        return 0;
    } 
    else {
        fprintf(stderr, COLOR_ERROR "Error: Modo '%s' no reconocido.\n\n" COLOR_RESET, mode);
        print_usage(argv[0]);
        return 1;
    }

    /* Mostrar el resultado final parseado en la estructura de datos */
    print_config_result(&config);

    /* DETECTAR ARGUMENTOS POSICIONALES ADICIONALES (Non-option arguments):
     * getopt y getopt_long reordenan argv desplazando los argumentos libres
     * (por ejemplo, nombres de archivos a procesar) al final del vector.
     * Estos quedan accesibles desde el índice optind hasta argc - 1.
     */
    if (optind < argc && strcmp(mode, "manual") != 0) {
        printf(COLOR_TITLE "Argumentos posicionales adicionales detectados (optind = %d):\n" COLOR_RESET, optind);
        for (int i = optind; i < argc; i++) {
            printf("  argv[%d] = \"%s\" (posicional)\n", i, argv[i]);
        }
        printf("\n");
    }

    return 0;
}
