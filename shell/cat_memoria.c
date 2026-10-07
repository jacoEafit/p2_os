#include "shell.h"
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * ====================================================================================
 * COMANDO: m_sbrk <incremento_bytes>
 * ====================================================================================
 * Demuestra el crecimiento de la memoria dinámica (heap) del proceso.
 * 
 * Explicación teórica:
 * Tradicionalmente, la memoria dinámica del heap en Unix tiene una frontera llamada
 * "program break". Detrás de esta dirección se encuentra la memoria física asignada al heap.
 * 
 * Syscalls explicadas:
 * 1. sbrk(2): Modifica el límite del segmento de datos del proceso (program break).
 *    - sbrk(0): Devuelve la dirección actual del program break sin modificarla.
 *    - sbrk(increment): Suma 'increment' bytes al program break actual, reservando
 *      físicamente memoria en el heap o devolviéndola al kernel si es negativo.
 *      Devuelve la dirección del program break *anterior*.
 */
int cmd_m_sbrk(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: m_sbrk <incremento_bytes>\n" COLOR_RESET);
        return 1;
    }
    long increment = strtol(argv[1], NULL, 10);

    /* 1. Obtener la dirección actual del program break sin cambiar nada */
    LOG_SYSCALL("sbrk", "0");
    void *current_break = sbrk(0);
    if (current_break == (void *)-1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT_PTR(current_break);

    /* 2. Modificar la dirección de corte (program break) */
    LOG_SYSCALL("sbrk", "%ld", increment);
    void *new_break = sbrk(increment);
    if (new_break == (void *)-1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT_PTR(new_break); /* Retorna el break de antes de aplicar el incremento */

    /* 3. Obtener el nuevo program break para confirmar que se aplicó el incremento */
    LOG_SYSCALL("sbrk", "0");
    void *confirmed_break = sbrk(0);
    if (confirmed_break == (void *)-1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT_PTR(confirmed_break);

    printf(COLOR_RESULT "Dirección anterior del break: %p\n" COLOR_RESET, current_break);
    printf(COLOR_RESULT "Dirección nueva del break:    %p\n" COLOR_RESET, confirmed_break);
    printf(COLOR_INFO "El heap cambió en %ld bytes.\n" COLOR_RESET, (long)((char *)confirmed_break - (char *)current_break));

    return 0;
}

/**
 * ====================================================================================
 * COMANDO: m_mmap <tamaño_bytes>
 * ====================================================================================
 * Demuestra la reserva directa de páginas de memoria virtual anónima.
 * 
 * Explicación teórica:
 * Para reservas de memoria grandes (usualmente > 128KB en malloc), el sistema operativo
 * prefiere usar mmap en lugar de sbrk para mapear páginas físicas completas de forma anónima
 * y evitar la fragmentación en el heap.
 * 
 * Syscalls explicadas:
 * 1. mmap(2): Mapea archivos o dispositivos en memoria virtual, o mapea memoria en blanco (anónima).
 *    - addr = NULL: El kernel elige la dirección virtual de alineación óptima.
 *    - size: Cantidad de bytes a reservar (se redondea al tamaño de página del sistema, e.g. 4KB).
 *    - PROT_READ|PROT_WRITE: Permisos de protección (Lectura y Escritura permitidas en las páginas).
 *    - MAP_PRIVATE: Los cambios no se comparten con otros procesos.
 *    - MAP_ANONYMOUS: No hay archivo físico de respaldo. La memoria se inicializa en 0.
 *    - fd = -1, offset = 0: Requerido cuando se usa mapeo anónimo.
 * 2. munmap(2): Libera las páginas virtuales mapeadas devolviéndolas al kernel.
 */
int cmd_m_mmap(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: m_mmap <tamaño_bytes>\n" COLOR_RESET);
        return 1;
    }
    size_t size = (size_t)strtoul(argv[1], NULL, 10);
    if (size == 0) {
        fprintf(stderr, COLOR_ERROR "El tamaño debe ser mayor que 0.\n" COLOR_RESET);
        return 1;
    }

    /* 1. Mapear memoria virtual */
    LOG_SYSCALL("mmap", "NULL, %zu, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0", size);
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT_PTR(addr);

    printf(COLOR_INFO "Memoria mapeada en la dirección: %p\n" COLOR_RESET, addr);
    printf(COLOR_INFO "Escribiendo patrón en la memoria mapeada...\n" COLOR_RESET);
    
    /* Escribir un patrón en las páginas mapeadas para verificar que se puede leer/escribir físicamente */
    char *ptr = (char *)addr;
    for (size_t i = 0; i < size - 1; i++) {
        ptr[i] = 'A' + (i % 26);
    }
    ptr[size - 1] = '\0';

    if (size > 50) {
        printf(COLOR_INFO "Muestra del contenido (primeros 40 caracteres): %.40s...\n" COLOR_RESET, ptr);
    } else {
        printf(COLOR_INFO "Contenido: %s\n" COLOR_RESET, ptr);
    }

    /* 2. Desmapear (liberar) la memoria mapeada */
    LOG_SYSCALL("munmap", "%p, %zu", addr, size);
    int res = munmap(addr, size);
    if (res == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(res);

    printf(COLOR_RESULT "Memoria desmapeada exitosamente.\n" COLOR_RESET);
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: m_info
 * ====================================================================================
 * Lee estadísticas del estado del mapa de memoria del proceso.
 * 
 * Explicación teórica:
 * El sistema de archivos virtual /proc en Linux expone información del kernel como ficheros.
 * `/proc/self/status` expone información detallada de estado del proceso actual (self).
 * Leer este archivo lee dinámicamente metadatos del espacio de direcciones del kernel.
 */
int cmd_m_info(int argc, char **argv) {
    (void)argc;
    (void)argv;
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) {
        perror("fopen /proc/self/status");
        return 1;
    }

    printf(COLOR_TITLE "--- Estado de Memoria del Shell (/proc/self/status) ---\n" COLOR_RESET);
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Filtramos solo líneas de estadísticas de memoria interesantes */
        if (strncmp(line, "VmPeak:", 7) == 0 ||    /* Pico de memoria virtual ocupada */
            strncmp(line, "VmSize:", 7) == 0 ||    /* Memoria virtual total asignada */
            strncmp(line, "VmLck:", 6) == 0 ||     /* Páginas bloqueadas físicamente en RAM (mlock) */
            strncmp(line, "VmHWM:", 6) == 0 ||     /* Pico de memoria física residente (High Water Mark) */
            strncmp(line, "VmRSS:", 6) == 0 ||     /* Memoria física residente (Resident Set Size) actualmente ocupada en RAM */
            strncmp(line, "VmData:", 7) == 0 ||    /* Tamaño del segmento de datos (heap) */
            strncmp(line, "VmStk:", 6) == 0 ||     /* Tamaño del segmento de pila (stack) */
            strncmp(line, "VmExe:", 6) == 0 ||     /* Tamaño del segmento de código de texto */
            strncmp(line, "VmLib:", 6) == 0 ||     /* Memoria asignada a librerías compartidas asociadas */
            strncmp(line, "VmPTE:", 6) == 0) {     /* Tamaño que ocupan las tablas de páginas de traducción */
            printf("  %s", line);
        }
    }
    fclose(f);
    printf(COLOR_TITLE "--------------------------------------------------------\n" COLOR_RESET);

    return 0;
}
