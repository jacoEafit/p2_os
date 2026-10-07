#include "shell.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#define NUM_SIMBOLOS     256
#define MAX_NODOS        (2 * NUM_SIMBOLOS - 1)
#define MAX_LONG_CODIGO  64
#define MAGIA_HUF        "HUF1"
#define MAX_HILOS        64
#define MIN_TAM_BLOQUE   1024
#define MAX_TAM_BLOQUE   (64 * 1024 * 1024)
#define ANCHO_BARRA      30

static int    g_num_hilos  = 4;
static size_t g_tam_bloque = 64 * 1024;

typedef enum {
    FASE_CONTEO,
    FASE_ARBOL,
    FASE_CODIFICACION,
    FASE_TERMINADO,
    FASE_ERROR
} FaseCompresor;

typedef struct {
    uint64_t frecuencia;
    int izq;
    int der;
    int simbolo;
} NodoHuffman;

typedef struct {
    uint64_t bits;
    int longitud;
} CodigoHuffman;

typedef struct {
    unsigned char *datos;
    size_t tam_bytes;
    uint64_t num_bits;
    int listo;
} BloqueComprimido;

typedef struct {
    char magia[4];
    uint32_t tam_bloque;
    uint64_t tam_original;
    uint32_t num_bloques;
    uint32_t reservado;
    uint64_t frecuencias[NUM_SIMBOLOS];
} CabeceraHuf;

typedef struct {
    char ruta_entrada[PATH_MAX];
    char ruta_salida[PATH_MAX];
    int fd_entrada;
    int fd_salida;
    uint64_t tam_archivo;
    size_t tam_bloque;
    size_t num_bloques;
    int num_hilos;
    size_t ventana;

    uint64_t frecuencias[NUM_SIMBOLOS];
    NodoHuffman nodos[MAX_NODOS];
    int raiz;
    CodigoHuffman codigos[NUM_SIMBOLOS];
    BloqueComprimido *bloques;

    size_t siguiente_bloque;
    size_t bloques_contados;
    size_t bloques_codificados;
    size_t bloques_escritos;
    uint64_t bytes_salida;
    FaseCompresor fase;
    int abortar;
    char mensaje_error[160];
    struct timespec inicio;
    struct timespec fin;

    pthread_mutex_t mutex;
    pthread_cond_t cond_bloque_listo;
    pthread_cond_t cond_espacio;

    pthread_t hilo_coordinador;
} TrabajoCompresion;

static TrabajoCompresion *g_trabajo = NULL;
static pthread_mutex_t g_mutex_trabajo = PTHREAD_MUTEX_INITIALIZER;

static int escribir_todo(int fd, const void *buf, size_t len) {
    const unsigned char *p = buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static size_t tam_de_bloque(const TrabajoCompresion *t, size_t i) {
    uint64_t inicio = (uint64_t)i * t->tam_bloque;
    uint64_t resto = t->tam_archivo - inicio;
    return resto < t->tam_bloque ? (size_t)resto : t->tam_bloque;
}

static ssize_t leer_bloque(const TrabajoCompresion *t, size_t i, unsigned char *buf) {
    size_t total = tam_de_bloque(t, i);
    off_t offset = (off_t)((uint64_t)i * t->tam_bloque);
    size_t leidos = 0;
    while (leidos < total) {
        ssize_t n = pread(t->fd_entrada, buf + leidos, total - leidos, offset + (off_t)leidos);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        leidos += (size_t)n;
    }
    return (ssize_t)leidos;
}

static double segundos_desde(const struct timespec *desde, const struct timespec *hasta) {
    return (double)(hasta->tv_sec - desde->tv_sec) + (double)(hasta->tv_nsec - desde->tv_nsec) / 1e9;
}

static void marcar_error(TrabajoCompresion *t, const char *mensaje) {
    pthread_mutex_lock(&t->mutex);
    if (!t->abortar) {
        snprintf(t->mensaje_error, sizeof(t->mensaje_error), "%s", mensaje);
    }
    t->abortar = 1;
    pthread_cond_broadcast(&t->cond_bloque_listo);
    pthread_cond_broadcast(&t->cond_espacio);
    pthread_mutex_unlock(&t->mutex);
}

static int tomar_siguiente_bloque(TrabajoCompresion *t, size_t *indice) {
    int hay = 0;
    pthread_mutex_lock(&t->mutex);
    if (!t->abortar && t->siguiente_bloque < t->num_bloques) {
        *indice = t->siguiente_bloque++;
        hay = 1;
    }
    pthread_mutex_unlock(&t->mutex);
    return hay;
}

static int construir_arbol(const uint64_t frecuencias[NUM_SIMBOLOS], NodoHuffman nodos[MAX_NODOS]) {
    int activo[MAX_NODOS] = {0};
    int num_nodos = 0;

    for (int s = 0; s < NUM_SIMBOLOS; s++) {
        if (frecuencias[s] > 0) {
            nodos[num_nodos] = (NodoHuffman){ frecuencias[s], -1, -1, s };
            activo[num_nodos] = 1;
            num_nodos++;
        }
    }
    if (num_nodos == 0) return -1;

    int activos = num_nodos;
    while (activos > 1) {
        int a = -1, b = -1;
        for (int i = 0; i < num_nodos; i++) {
            if (!activo[i]) continue;
            if (a == -1 || nodos[i].frecuencia < nodos[a].frecuencia) {
                b = a;
                a = i;
            } else if (b == -1 || nodos[i].frecuencia < nodos[b].frecuencia) {
                b = i;
            }
        }
        nodos[num_nodos] = (NodoHuffman){ nodos[a].frecuencia + nodos[b].frecuencia, a, b, -1 };
        activo[a] = activo[b] = 0;
        activo[num_nodos] = 1;
        num_nodos++;
        activos--;
    }
    return num_nodos - 1;
}

static int generar_codigos(const NodoHuffman nodos[MAX_NODOS], int nodo, uint64_t bits, int longitud,
                           CodigoHuffman codigos[NUM_SIMBOLOS]) {
    if (nodos[nodo].izq == -1) {
        codigos[nodos[nodo].simbolo] = (CodigoHuffman){ bits, longitud > 0 ? longitud : 1 };
        return 0;
    }
    if (longitud >= MAX_LONG_CODIGO) return -1;
    if (generar_codigos(nodos, nodos[nodo].izq, bits << 1, longitud + 1, codigos) != 0) return -1;
    return generar_codigos(nodos, nodos[nodo].der, (bits << 1) | 1, longitud + 1, codigos);
}

static int codificar_bloque(const TrabajoCompresion *t, const unsigned char *entrada, size_t n,
                            BloqueComprimido *salida) {
    uint64_t total_bits = 0;
    for (size_t k = 0; k < n; k++) {
        total_bits += (uint64_t)t->codigos[entrada[k]].longitud;
    }
    size_t tam_bytes = (size_t)((total_bits + 7) / 8);

    unsigned char *datos = calloc(tam_bytes > 0 ? tam_bytes : 1, 1);
    if (datos == NULL) return -1;

    uint64_t pos = 0;
    for (size_t k = 0; k < n; k++) {
        CodigoHuffman c = t->codigos[entrada[k]];
        for (int b = c.longitud - 1; b >= 0; b--) {
            if ((c.bits >> b) & 1) {
                datos[pos >> 3] |= (unsigned char)(0x80 >> (pos & 7));
            }
            pos++;
        }
    }

    salida->datos = datos;
    salida->tam_bytes = tam_bytes;
    salida->num_bits = total_bits;
    return 0;
}

static void *hilo_conteo(void *arg) {
    TrabajoCompresion *t = arg;
    uint64_t locales[NUM_SIMBOLOS] = {0};

    unsigned char *buf = malloc(t->tam_bloque);
    if (buf == NULL) {
        marcar_error(t, "malloc: sin memoria para el bloque");
        return NULL;
    }

    size_t i;
    while (tomar_siguiente_bloque(t, &i)) {
        ssize_t n = leer_bloque(t, i, buf);
        if (n < 0) {
            marcar_error(t, strerror(errno));
            break;
        }
        for (ssize_t k = 0; k < n; k++) {
            locales[buf[k]]++;
        }
        pthread_mutex_lock(&t->mutex);
        t->bloques_contados++;
        pthread_mutex_unlock(&t->mutex);
    }

    pthread_mutex_lock(&t->mutex);
    for (int s = 0; s < NUM_SIMBOLOS; s++) {
        t->frecuencias[s] += locales[s];
    }
    pthread_mutex_unlock(&t->mutex);

    free(buf);
    return NULL;
}

static void *hilo_codificacion(void *arg) {
    TrabajoCompresion *t = arg;

    unsigned char *buf = malloc(t->tam_bloque);
    if (buf == NULL) {
        marcar_error(t, "malloc: sin memoria para el bloque");
        return NULL;
    }

    size_t i;
    while (tomar_siguiente_bloque(t, &i)) {
        pthread_mutex_lock(&t->mutex);
        while (!t->abortar && i >= t->bloques_escritos + t->ventana) {
            pthread_cond_wait(&t->cond_espacio, &t->mutex);
        }
        int abortar = t->abortar;
        pthread_mutex_unlock(&t->mutex);
        if (abortar) break;

        ssize_t n = leer_bloque(t, i, buf);
        if (n < 0) {
            marcar_error(t, strerror(errno));
            break;
        }

        BloqueComprimido resultado;
        if (codificar_bloque(t, buf, (size_t)n, &resultado) != 0) {
            marcar_error(t, "malloc: sin memoria para el bloque codificado");
            break;
        }

        pthread_mutex_lock(&t->mutex);
        t->bloques[i] = resultado;
        t->bloques[i].listo = 1;
        t->bloques_codificados++;
        pthread_cond_signal(&t->cond_bloque_listo);
        pthread_mutex_unlock(&t->mutex);
    }

    free(buf);
    return NULL;
}

static void ejecutar_pool(TrabajoCompresion *t, void *(*rutina)(void *)) {
    pthread_t hilos[t->num_hilos];
    int creados = 0;
    for (int h = 0; h < t->num_hilos; h++) {
        if (pthread_create(&hilos[h], NULL, rutina, t) != 0) {
            marcar_error(t, "pthread_create: no se pudo crear un hilo trabajador");
            break;
        }
        creados++;
    }
    for (int h = 0; h < creados; h++) {
        pthread_join(hilos[h], NULL);
    }
}

static void escribir_bloques_en_orden(TrabajoCompresion *t) {
    for (size_t i = 0; i < t->num_bloques; i++) {
        pthread_mutex_lock(&t->mutex);
        while (!t->abortar && !t->bloques[i].listo) {
            pthread_cond_wait(&t->cond_bloque_listo, &t->mutex);
        }
        if (t->abortar) {
            pthread_mutex_unlock(&t->mutex);
            return;
        }
        BloqueComprimido b = t->bloques[i];
        t->bloques[i].datos = NULL;
        pthread_mutex_unlock(&t->mutex);

        int fallo = escribir_todo(t->fd_salida, &b.num_bits, sizeof(b.num_bits)) != 0 ||
                    escribir_todo(t->fd_salida, b.datos, b.tam_bytes) != 0;
        free(b.datos);
        if (fallo) {
            marcar_error(t, strerror(errno));
            return;
        }

        pthread_mutex_lock(&t->mutex);
        t->bloques_escritos++;
        t->bytes_salida += sizeof(b.num_bits) + b.tam_bytes;
        pthread_cond_broadcast(&t->cond_espacio);
        pthread_mutex_unlock(&t->mutex);
    }
}

static void cambiar_fase(TrabajoCompresion *t, FaseCompresor fase) {
    pthread_mutex_lock(&t->mutex);
    t->fase = fase;
    t->siguiente_bloque = 0;
    pthread_mutex_unlock(&t->mutex);
}

static int hay_que_abortar(TrabajoCompresion *t) {
    pthread_mutex_lock(&t->mutex);
    int abortar = t->abortar;
    pthread_mutex_unlock(&t->mutex);
    return abortar;
}

static void *hilo_coordinador_compresion(void *arg) {
    TrabajoCompresion *t = arg;

    ejecutar_pool(t, hilo_conteo);

    if (!hay_que_abortar(t)) {
        cambiar_fase(t, FASE_ARBOL);
        t->raiz = construir_arbol(t->frecuencias, t->nodos);
        if (t->raiz >= 0 && generar_codigos(t->nodos, t->raiz, 0, 0, t->codigos) != 0) {
            marcar_error(t, "código de Huffman demasiado largo");
        }
    }

    if (!hay_que_abortar(t)) {
        CabeceraHuf cab;
        memset(&cab, 0, sizeof(cab));
        memcpy(cab.magia, MAGIA_HUF, sizeof(cab.magia));
        cab.tam_bloque = (uint32_t)t->tam_bloque;
        cab.tam_original = t->tam_archivo;
        cab.num_bloques = (uint32_t)t->num_bloques;
        memcpy(cab.frecuencias, t->frecuencias, sizeof(cab.frecuencias));
        if (escribir_todo(t->fd_salida, &cab, sizeof(cab)) != 0) {
            marcar_error(t, strerror(errno));
        } else {
            pthread_mutex_lock(&t->mutex);
            t->bytes_salida = sizeof(cab);
            pthread_mutex_unlock(&t->mutex);
        }
    }

    if (!hay_que_abortar(t)) {
        cambiar_fase(t, FASE_CODIFICACION);
        pthread_t hilos[t->num_hilos];
        int creados = 0;
        for (int h = 0; h < t->num_hilos; h++) {
            if (pthread_create(&hilos[h], NULL, hilo_codificacion, t) != 0) {
                marcar_error(t, "pthread_create: no se pudo crear un hilo trabajador");
                break;
            }
            creados++;
        }
        if (creados > 0) {
            escribir_bloques_en_orden(t);
        }
        for (int h = 0; h < creados; h++) {
            pthread_join(hilos[h], NULL);
        }
    }

    for (size_t i = 0; i < t->num_bloques; i++) {
        free(t->bloques[i].datos);
        t->bloques[i].datos = NULL;
    }
    close(t->fd_entrada);
    if (close(t->fd_salida) != 0) {
        marcar_error(t, strerror(errno));
    }

    int error = hay_que_abortar(t);
    if (error) {
        unlink(t->ruta_salida);
    }

    pthread_mutex_lock(&t->mutex);
    clock_gettime(CLOCK_MONOTONIC, &t->fin);
    uint64_t bytes_salida = t->bytes_salida;
    pthread_mutex_unlock(&t->mutex);

    if (error) {
        printf("\n" COLOR_ERROR "[compresor] Error al comprimir '%s': %s\n" COLOR_RESET,
               t->ruta_entrada, t->mensaje_error);
    } else {
        double ratio = t->tam_archivo > 0 ? 100.0 * (double)bytes_salida / (double)t->tam_archivo : 0.0;
        printf("\n" COLOR_RESULT "[compresor] Terminado: %s -> %s (%llu -> %llu bytes, %.1f%%) en %.3f s\n" COLOR_RESET,
               t->ruta_entrada, t->ruta_salida,
               (unsigned long long)t->tam_archivo, (unsigned long long)bytes_salida, ratio,
               segundos_desde(&t->inicio, &t->fin));
    }
    printf(COLOR_PROMPT "eafitOS> " COLOR_RESET);
    fflush(stdout);

    pthread_mutex_lock(&t->mutex);
    t->fase = error ? FASE_ERROR : FASE_TERMINADO;
    pthread_mutex_unlock(&t->mutex);
    return NULL;
}

static void liberar_trabajo(TrabajoCompresion *t) {
    pthread_mutex_destroy(&t->mutex);
    pthread_cond_destroy(&t->cond_bloque_listo);
    pthread_cond_destroy(&t->cond_espacio);
    free(t->bloques);
    free(t);
}

static int trabajo_en_curso(void) {
    if (g_trabajo == NULL) return 0;

    pthread_mutex_lock(&g_trabajo->mutex);
    FaseCompresor fase = g_trabajo->fase;
    pthread_mutex_unlock(&g_trabajo->mutex);
    return fase != FASE_TERMINADO && fase != FASE_ERROR;
}

static int recoger_trabajo_terminado(void) {
    if (g_trabajo == NULL) return 0;
    if (trabajo_en_curso()) return 1;

    pthread_join(g_trabajo->hilo_coordinador, NULL);
    liberar_trabajo(g_trabajo);
    g_trabajo = NULL;
    return 0;
}

void compresor_finalizar(void) {
    pthread_mutex_lock(&g_mutex_trabajo);
    if (recoger_trabajo_terminado()) {
        printf(COLOR_INFO "[compresor] Esperando a que termine el trabajo en curso...\n" COLOR_RESET);
        fflush(stdout);
        pthread_join(g_trabajo->hilo_coordinador, NULL);
        liberar_trabajo(g_trabajo);
        g_trabajo = NULL;
    }
    pthread_mutex_unlock(&g_mutex_trabajo);
}

int cmd_c_comprimir(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: c_comprimir <archivo>\n" COLOR_RESET);
        return 1;
    }
    const char *archivo = argv[1];

    pthread_mutex_lock(&g_mutex_trabajo);
    if (recoger_trabajo_terminado()) {
        pthread_mutex_unlock(&g_mutex_trabajo);
        printf(COLOR_ERROR "[compresor] Ya hay un trabajo en curso. Espera a que termine.\n" COLOR_RESET);
        return 1;
    }

    TrabajoCompresion *t = calloc(1, sizeof(TrabajoCompresion));
    if (t == NULL) {
        pthread_mutex_unlock(&g_mutex_trabajo);
        printf(COLOR_ERROR "malloc: sin memoria disponible.\n" COLOR_RESET);
        return 1;
    }
    t->fd_entrada = -1;
    t->fd_salida = -1;
    snprintf(t->ruta_entrada, sizeof(t->ruta_entrada), "%s", archivo);
    if (snprintf(t->ruta_salida, sizeof(t->ruta_salida), "%s.huf", archivo) >= (int)sizeof(t->ruta_salida)) {
        printf(COLOR_ERROR "[compresor] Ruta demasiado larga.\n" COLOR_RESET);
        goto error;
    }

    LOG_SYSCALL("open", "\"%s\", O_RDONLY", t->ruta_entrada);
    t->fd_entrada = open(t->ruta_entrada, O_RDONLY);
    if (t->fd_entrada == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        goto error;
    }
    LOG_SYSCALL_RESULT(t->fd_entrada);

    struct stat st;
    LOG_SYSCALL("fstat", "%d, &st", t->fd_entrada);
    if (fstat(t->fd_entrada, &st) == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        goto error;
    }
    LOG_SYSCALL_RESULT(0);
    if (!S_ISREG(st.st_mode)) {
        printf(COLOR_ERROR "[compresor] '%s' no es un archivo regular.\n" COLOR_RESET, archivo);
        goto error;
    }

    t->tam_archivo = (uint64_t)st.st_size;
    t->tam_bloque = g_tam_bloque;
    t->num_bloques = (size_t)((t->tam_archivo + t->tam_bloque - 1) / t->tam_bloque);
    t->num_hilos = g_num_hilos;
    if ((size_t)t->num_hilos > t->num_bloques) {
        t->num_hilos = t->num_bloques > 0 ? (int)t->num_bloques : 1;
    }
    t->ventana = 2 * (size_t)t->num_hilos;
    t->fase = FASE_CONTEO;

    t->bloques = calloc(t->num_bloques > 0 ? t->num_bloques : 1, sizeof(BloqueComprimido));
    if (t->bloques == NULL) {
        printf(COLOR_ERROR "malloc: sin memoria disponible.\n" COLOR_RESET);
        goto error;
    }

    LOG_SYSCALL("open", "\"%s\", O_WRONLY|O_CREAT|O_TRUNC, 0644", t->ruta_salida);
    t->fd_salida = open(t->ruta_salida, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (t->fd_salida == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        goto error;
    }
    LOG_SYSCALL_RESULT(t->fd_salida);

    pthread_mutex_init(&t->mutex, NULL);
    pthread_cond_init(&t->cond_bloque_listo, NULL);
    pthread_cond_init(&t->cond_espacio, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t->inicio);

    LOG_SYSCALL("pthread_create", "coordinador");
    int rc = pthread_create(&t->hilo_coordinador, NULL, hilo_coordinador_compresion, t);
    if (rc != 0) {
        LOG_SYSCALL_ERROR(strerror(rc));
        pthread_mutex_destroy(&t->mutex);
        pthread_cond_destroy(&t->cond_bloque_listo);
        pthread_cond_destroy(&t->cond_espacio);
        unlink(t->ruta_salida);
        goto error;
    }
    LOG_SYSCALL_RESULT(0);

    g_trabajo = t;
    pthread_mutex_unlock(&g_mutex_trabajo);

    printf(COLOR_INFO "[compresor] Trabajo iniciado: %s -> %s (%zu bloques de %zu bytes, %d hilos)\n" COLOR_RESET,
           t->ruta_entrada, t->ruta_salida, t->num_bloques, t->tam_bloque, t->num_hilos);
    return 0;

error:
    if (t->fd_entrada != -1) close(t->fd_entrada);
    if (t->fd_salida != -1) close(t->fd_salida);
    free(t->bloques);
    free(t);
    pthread_mutex_unlock(&g_mutex_trabajo);
    return 1;
}

/**
 * ====================================================================================
 * COMANDO: c_descomprimir
 * ====================================================================================
 * Descomprime un archivo .huf en segundo plano.
 *
 * (Pendiente de implementar)
 */
int cmd_c_descomprimir(int argc, char **argv) {
    (void)argc;
    (void)argv;

    return 0;
}

static const char *nombre_fase(FaseCompresor fase) {
    switch (fase) {
        case FASE_CONTEO:       return "1/3 conteo de frecuencias";
        case FASE_ARBOL:        return "2/3 construcción del árbol";
        case FASE_CODIFICACION: return "3/3 codificación y escritura";
        case FASE_TERMINADO:    return "terminado";
        case FASE_ERROR:        return "error";
    }
    return "desconocida";
}

static void imprimir_barra(double porcentaje) {
    int llenos = (int)(porcentaje / 100.0 * ANCHO_BARRA);
    printf("  Progreso:  [" COLOR_RESULT);
    for (int i = 0; i < ANCHO_BARRA; i++) {
        putchar(i < llenos ? '#' : '-');
    }
    printf(COLOR_RESET "] %5.1f%%\n", porcentaje);
}

int cmd_c_estado(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, COLOR_ERROR "Uso: c_estado\n" COLOR_RESET);
        return 1;
    }

    pthread_mutex_lock(&g_mutex_trabajo);
    TrabajoCompresion *t = g_trabajo;
    if (t == NULL) {
        pthread_mutex_unlock(&g_mutex_trabajo);
        printf(COLOR_INFO "[compresor] No hay ningún trabajo en curso ni terminado.\n" COLOR_RESET);
        return 0;
    }

    pthread_mutex_lock(&t->mutex);
    FaseCompresor fase = t->fase;
    size_t contados = t->bloques_contados;
    size_t codificados = t->bloques_codificados;
    size_t escritos = t->bloques_escritos;
    uint64_t bytes_salida = t->bytes_salida;
    struct timespec fin = t->fin;
    char mensaje_error[sizeof(t->mensaje_error)];
    memcpy(mensaje_error, t->mensaje_error, sizeof(mensaje_error));
    pthread_mutex_unlock(&t->mutex);

    struct timespec ahora;
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    int en_curso = fase != FASE_TERMINADO && fase != FASE_ERROR;
    double transcurrido = segundos_desde(&t->inicio, en_curso ? &ahora : &fin);

    double porcentaje;
    size_t n = t->num_bloques;
    if (fase == FASE_TERMINADO || n == 0) {
        porcentaje = 100.0;
    } else if (fase == FASE_CONTEO) {
        porcentaje = 50.0 * (double)contados / (double)n;
    } else {
        porcentaje = 50.0 + 50.0 * (double)escritos / (double)n;
    }

    printf(COLOR_TITLE "\n--- Estado del compresor ---\n" COLOR_RESET);
    printf("  Archivo:   %s -> %s\n", t->ruta_entrada, t->ruta_salida);
    printf("  Fase:      " COLOR_PARAM "%s" COLOR_RESET "\n", nombre_fase(fase));
    if (fase != FASE_ERROR) {
        imprimir_barra(porcentaje);
    }
    printf("  Bloques:   contados %zu/%zu, codificados %zu/%zu, escritos %zu/%zu\n",
           contados, n, codificados, n, escritos, n);
    printf("  Config:    %d hilos, bloques de %zu bytes\n", t->num_hilos, t->tam_bloque);
    printf("  Tamaño:    %llu -> %llu bytes\n",
           (unsigned long long)t->tam_archivo, (unsigned long long)bytes_salida);
    printf("  Tiempo:    %.3f s\n", transcurrido);
    if (fase == FASE_ERROR) {
        printf("  " COLOR_ERROR "Error:     %s" COLOR_RESET "\n", mensaje_error);
    }

    pthread_mutex_unlock(&g_mutex_trabajo);
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: c_cancelar
 * ====================================================================================
 * Cancela el trabajo en curso, termina los hilos y borra la salida parcial.
 *
 * (Pendiente de implementar)
 */
int cmd_c_cancelar(int argc, char **argv) {
    (void)argc;
    (void)argv;

    return 0;
}

/**
 * ====================================================================================
 * COMANDO: c_verificar
 * ====================================================================================
 * Compara el checksum de dos archivos para comprobar la integridad.
 *
 * (Pendiente de implementar)
 */
int cmd_c_verificar(int argc, char **argv) {
    (void)argc;
    (void)argv;

    return 0;
}

static int leer_tamano(const char *texto, size_t *resultado) {
    char *fin;
    errno = 0;
    unsigned long long valor = strtoull(texto, &fin, 10);
    if (errno != 0 || fin == texto || texto[0] == '-') return -1;

    unsigned long long multiplicador = 1;
    if (*fin == 'K' || *fin == 'k') {
        multiplicador = 1024ULL;
        fin++;
    } else if (*fin == 'M' || *fin == 'm') {
        multiplicador = 1024ULL * 1024ULL;
        fin++;
    }
    if (*fin != '\0' || valor > MAX_TAM_BLOQUE / multiplicador) return -1;

    *resultado = (size_t)(valor * multiplicador);
    return 0;
}

int cmd_c_config(int argc, char **argv) {
    if (argc != 1 && argc != 3) {
        fprintf(stderr, COLOR_ERROR "Uso: c_config [<hilos> <tam_bloque>]  (tam_bloque en bytes, admite K o M)\n" COLOR_RESET);
        return 1;
    }

    pthread_mutex_lock(&g_mutex_trabajo);

    if (argc == 3) {
        char *fin;
        errno = 0;
        long hilos = strtol(argv[1], &fin, 10);
        size_t tam_bloque;

        if (errno != 0 || *fin != '\0' || hilos < 1 || hilos > MAX_HILOS) {
            pthread_mutex_unlock(&g_mutex_trabajo);
            printf(COLOR_ERROR "[compresor] Número de hilos inválido: debe estar entre 1 y %d.\n" COLOR_RESET, MAX_HILOS);
            return 1;
        }
        if (leer_tamano(argv[2], &tam_bloque) != 0 || tam_bloque < MIN_TAM_BLOQUE) {
            pthread_mutex_unlock(&g_mutex_trabajo);
            printf(COLOR_ERROR "[compresor] Tamaño de bloque inválido: debe estar entre %d bytes y %d MB.\n" COLOR_RESET,
                   MIN_TAM_BLOQUE, MAX_TAM_BLOQUE / (1024 * 1024));
            return 1;
        }

        g_num_hilos = (int)hilos;
        g_tam_bloque = tam_bloque;
        printf(COLOR_RESULT "[compresor] Configuración actualizada.\n" COLOR_RESET);
        if (trabajo_en_curso()) {
            printf(COLOR_INFO "[compresor] El trabajo en curso conserva su configuración; la nueva aplica al siguiente.\n" COLOR_RESET);
        }
    }

    printf("  Hilos:          " COLOR_PARAM "%d" COLOR_RESET "\n", g_num_hilos);
    printf("  Tamaño bloque:  " COLOR_PARAM "%zu bytes" COLOR_RESET "\n", g_tam_bloque);

    pthread_mutex_unlock(&g_mutex_trabajo);
    return 0;
}
