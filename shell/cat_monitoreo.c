#include "shell.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include <errno.h>

/**
 * ====================================================================================
 * COMANDO: p_fork
 * ====================================================================================
 * Demuestra la bifurcación (clonación) y sincronización de procesos en Linux.
 * 
 * Explicación teórica:
 * Un proceso se clona a sí mismo creando una copia exacta llamada "hijo". 
 * Ambos continúan la ejecución a partir de la llamada fork().
 * 
 * Syscalls explicadas:
 * 1. fork(2): Clona el proceso llamador.
 *    - En el proceso PADRE: Retorna el PID (Process ID) del hijo creado (valor > 0).
 *    - En el proceso HIJO: Retorna el valor 0.
 *    - En caso de error: Retorna -1.
 * 2. getpid(2): Obtiene el identificador (PID) del proceso actual.
 * 3. getppid(2): Obtiene el identificador del proceso padre (Parent PID).
 * 4. waitpid(2): Suspende la ejecución del proceso actual hasta que el hijo indicado cambie de estado (e.g. termine).
 *    - Llena un entero 'status' que decodifica cómo terminó el proceso usando macros:
 *      - WIFEXITED: True si el hijo terminó de forma normal (e.g. exit() o return en main).
 *      - WEXITSTATUS: Extrae el código de salida que pasó el hijo en exit().
 */
int cmd_p_fork(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf(COLOR_INFO "Iniciando fork. El hijo dormirá 2 segundos y saldrá con código 42...\n" COLOR_RESET);

    /* 1. LLAMADA AL SISTEMA: fork */
    LOG_SYSCALL("fork", "");
    pid_t pid = fork();
    if (pid == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }

    if (pid == 0) {
        /* PROCESO HIJO (Su PID retornado es 0) */
        pid_t my_pid = getpid();
        pid_t my_ppid = getppid();
        /* Imprime su PID y el PPID de su padre */
        printf("\n" COLOR_CATEGORY "[Hijo (PID %d)]" COLOR_RESET " ¡Hola! Mi padre es %d. Dormiré 2 segundos...\n", my_pid, my_ppid);
        sleep(2); /* Simula una carga de trabajo durmiendo 2 segundos */
        printf(COLOR_CATEGORY "[Hijo (PID %d)]" COLOR_RESET " Desperté. Saliendo con código de estado 42...\n", my_pid);
        exit(42); /* El hijo termina. Envía el código de salida 42 al padre */
    } else {
        /* PROCESO PADRE (El PID retornado es el del hijo, ej. 31181) */
        LOG_SYSCALL_RESULT(pid);
        printf(COLOR_PROMPT "[Padre (PID %d)]" COLOR_RESET " Hijo creado con PID %d. Esperando a que termine...\n", getpid(), pid);

        int status;
        /* 2. LLAMADA AL SISTEMA: waitpid (bloqueante en este caso) */
        LOG_SYSCALL("waitpid", "%d, &status, 0", pid);
        pid_t waited_pid = waitpid(pid, &status, 0);
        if (waited_pid == -1) {
            LOG_SYSCALL_ERROR(strerror(errno));
            return 1;
        }
        LOG_SYSCALL_RESULT(waited_pid);

        /* Analizar cómo terminó el hijo usando macros especiales */
        if (WIFEXITED(status)) {
            printf(COLOR_PROMPT "[Padre]" COLOR_RESET " El hijo %d terminó normalmente con estado: " COLOR_RESULT "%d" COLOR_RESET "\n", waited_pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf(COLOR_PROMPT "[Padre]" COLOR_RESET " El hijo %d fue terminado por una señal de interrupción: " COLOR_ERROR "%d" COLOR_RESET "\n", waited_pid, WTERMSIG(status));
        }
    }
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: p_exec <comando> [argumentos...]
 * ====================================================================================
 * Explica cómo funcionan los shells tradicionales para ejecutar cualquier binario de Linux.
 * 
 * Explicación teórica:
 * El shell no sabe ejecutar comandos por sí mismo. Para correr 'ls', 'pwd' o cualquier comando, 
 * realiza un fork(), y en el hijo reemplaza su imagen de proceso usando execvp().
 * 
 * Syscalls explicadas:
 * 1. execvp(3) (Wrapper de execve(2)): Carga el binario indicado en el espacio de memoria virtual
 *    del proceso hijo, reemplazando su código, datos, pila y heap actuales.
 *    - Si tiene éxito, execvp() nunca retorna, ya que el proceso pasa a ejecutar el nuevo binario.
 *    - Si retorna -1, significa que falló (e.g. comando no encontrado).
 */
int cmd_p_exec(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, COLOR_ERROR "Uso: p_exec <comando> [argumentos...]\n" COLOR_RESET);
        return 1;
    }

    /* 1. Bifurcar el proceso shell */
    LOG_SYSCALL("fork", "");
    pid_t pid = fork();
    if (pid == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }

    if (pid == 0) {
        /* PROCESO HIJO */
        char **exec_args = &argv[1]; /* Los argumentos a ejecutar empiezan desde el índice 1 */
        
        /* 2. LLAMADA AL SISTEMA: execvp */
        LOG_SYSCALL("execvp", "\"%s\", argv", exec_args[0]);
        printf("\n" COLOR_INFO "[Hijo] Reemplazando imagen de proceso actual y ejecutando comando...\n\n" COLOR_RESET);
        
        execvp(exec_args[0], exec_args);
        
        /* SI execvp TIENE ÉXITO, ESTA LÍNEA NUNCA SE EJECUTA.
         * Si llegamos aquí, ocurrió un error (e.g. no existe el comando). */
        LOG_SYSCALL_ERROR(strerror(errno));
        fprintf(stderr, COLOR_ERROR "Error: No se pudo ejecutar el comando '%s'\n" COLOR_RESET, exec_args[0]);
        exit(127); /* Código estándar de shell para indicar comando no encontrado */
    } else {
        /* PROCESO PADRE */
        LOG_SYSCALL_RESULT(pid);
        printf(COLOR_PROMPT "[Padre]" COLOR_RESET " Esperando la ejecución de '%s' (PID %d)...\n", argv[1], pid);

        int status;
        /* 3. Esperar a que el proceso ejecutor termine */
        LOG_SYSCALL("waitpid", "%d, &status, 0", pid);
        pid_t waited_pid = waitpid(pid, &status, 0);
        if (waited_pid == -1) {
            LOG_SYSCALL_ERROR(strerror(errno));
            return 1;
        }
        LOG_SYSCALL_RESULT(waited_pid);

        if (WIFEXITED(status)) {
            printf(COLOR_PROMPT "[Padre]" COLOR_RESET " Comando terminado. Código de salida: " COLOR_RESULT "%d" COLOR_RESET "\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf(COLOR_PROMPT "[Padre]" COLOR_RESET " Comando terminado por señal: " COLOR_ERROR "%d" COLOR_RESET "\n", WTERMSIG(status));
        }
    }
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: p_kill <pid> <numero_señal>
 * ====================================================================================
 * Envía señales de comunicación entre procesos (IPC).
 * 
 * Syscalls explicadas:
 * 1. kill(2): Envía cualquier señal al proceso indicado.
 *    - pid: El proceso destino.
 *    - sig: El número de señal (e.g., 9 = SIGKILL terminación forzada, 15 = SIGTERM terminación limpia).
 */
int cmd_p_kill(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, COLOR_ERROR "Uso: p_kill <pid> <numero_señal>\n" COLOR_RESET);
        return 1;
    }
    pid_t pid = (pid_t)strtol(argv[1], NULL, 10);
    int sig = (int)strtol(argv[2], NULL, 10);

    /* 1. LLAMADA AL SISTEMA: kill */
    LOG_SYSCALL("kill", "%d, %d", pid, sig);
    int res = kill(pid, sig);
    if (res == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(res); /* Retorna 0 en caso de éxito */
    printf(COLOR_RESULT "Señal %d enviada al proceso %d exitosamente.\n" COLOR_RESET, sig, pid);
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: p_monitor
 * ====================================================================================
 * Muestra el perfil de consumo de recursos y contabilidad del shell.
 * 
 * Syscalls explicadas:
 * 1. getrusage(2): Devuelve métricas de uso de recursos para el proceso actual o sus hijos.
 *    - RUSAGE_SELF: Solicita métricas del propio proceso llamador.
 *    - usage: Estructura struct rusage que almacena tiempos de CPU, fallos de página, etc.
 */
int cmd_p_monitor(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct rusage usage;

    /* 1. LLAMADA AL SISTEMA: getrusage */
    LOG_SYSCALL("getrusage", "RUSAGE_SELF, &usage");
    int res = getrusage(RUSAGE_SELF, &usage);
    if (res == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(res);

    printf(COLOR_TITLE "--- Monitoreo de Recursos del Proceso (getrusage) ---\n" COLOR_RESET);
    /* ru_utime: Tiempo de CPU consumido ejecutando código en espacio de usuario */
    printf("  Tiempo de CPU (Usuario):    " COLOR_PARAM "%ld.%06ld s" COLOR_RESET "\n",
           usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
    /* ru_stime: Tiempo de CPU consumido ejecutando llamadas al sistema en espacio de kernel */
    printf("  Tiempo de CPU (Sistema):    " COLOR_PARAM "%ld.%06ld s" COLOR_RESET "\n",
           usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
    /* ru_maxrss: Consumo máximo de memoria RAM (tamaño residente) en KB */
    printf("  Max. Memoria Residente:     " COLOR_RESULT "%ld KB" COLOR_RESET "\n", usage.ru_maxrss);
    /* ru_minflt: Fallos de página que no requirieron E/S de disco (reutilización de páginas ya mapeadas) */
    printf("  Fallos de página menores:   %ld (reutilización de páginas físicas)\n", usage.ru_minflt);
    /* ru_majflt: Fallos de página duros que requirieron leer del disco (I/O) lento */
    printf("  Fallos de página mayores:   " COLOR_ERROR "%ld" COLOR_RESET " (requirieron I/O de disco)\n", usage.ru_majflt);
    /* ru_nvcsw: Cambios de contexto voluntarios (e.g. el proceso se durmió o cedió CPU por wait, sleep) */
    printf("  Cambios de contexto volunt.: %ld\n", usage.ru_nvcsw);
    /* ru_nivcsw: Cambios de contexto involuntarios (e.g. el planificador le quitó la CPU porque expiró su cuanto) */
    printf("  Cambios de contexto invol.:  %ld\n", usage.ru_nivcsw);
    printf(COLOR_TITLE "-----------------------------------------------------\n" COLOR_RESET);

    return 0;
}
