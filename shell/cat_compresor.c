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
#define FIRMA_HUF        "HUF1"
#define MAX_HILOS        64
#define MIN_TAM_BLOQUE   1024
#define MAX_TAM_BLOQUE   (64 * 1024 * 1024)
#define ANCHO_BARRA      30
#define MENSAJE_CANCELADO "cancelado por el usuario"

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
    uint64_t offset;
    int listo;
} BloqueComprimido;

typedef struct {
    // identifica el formato .huf
    char firma[4];
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
    uint64_t tam_entrada;
    dev_t dev_entrada, dev_salida;
    ino_t ino_entrada, ino_salida;
    int descomprimir;
    int cancelado;
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

    // un solo mutex protege el estado compartido del trabajo
    pthread_mutex_t mutex;
    pthread_cond_t cond_bloque_listo;
    pthread_cond_t cond_espacio;

    pthread_t hilo_coordinador;
} TrabajoCompresion;

static TrabajoCompresion *g_trabajo = NULL;
static pthread_mutex_t g_mutex_trabajo = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t g_mutex_salida = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond_monitor = PTHREAD_COND_INITIALIZER;
static pthread_t g_hilo_monitor;
static int g_monitor_activo = 0;
static int g_monitor_detener = 0;
static int g_esperando_entrada = 0;
static int g_prompt_con_progreso = 0;

static void monitor_detener(void);

// imprime el prompt; si recibe un resumen, lo muestra entre corchetes
static void imprimir_prompt_texto(const char *resumen) {
    if (resumen != NULL) {
        printf(COLOR_PROMPT "eafitOS " COLOR_PARAM "[%s]" COLOR_PROMPT "> " COLOR_RESET, resumen);
    } else {
        printf(COLOR_PROMPT "eafitOS> " COLOR_RESET);
    }
}

// write que reintenta ante escrituras parciales y EINTR
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

// tamaño real del bloque i (el último puede ser más corto)
static size_t tam_de_bloque(const TrabajoCompresion *t, size_t i) {
    uint64_t inicio = (uint64_t)i * t->tam_bloque;
    uint64_t resto = t->tam_archivo - inicio;
    return resto < t->tam_bloque ? (size_t)resto : t->tam_bloque;
}

// lee el bloque i de la entrada con pread, completando lecturas parciales
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

// lee len bytes desde un offset con pread; no comparte posición entre hilos, así que no necesita lock
static ssize_t leer_en(int fd, void *buf, size_t len, uint64_t offset) {
    unsigned char *p = buf;
    size_t leidos = 0;
    while (leidos < len) {
        ssize_t n = pread(fd, p + leidos, len - leidos, (off_t)(offset + leidos));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        leidos += (size_t)n;
    }
    return (ssize_t)leidos;
}

// diferencia en segundos entre dos marcas de tiempo
static double segundos_desde(const struct timespec *desde, const struct timespec *hasta) {
    return (double)(hasta->tv_sec - desde->tv_sec) + (double)(hasta->tv_nsec - desde->tv_nsec) / 1e9;
}

// guarda solo el primer error, activa abortar y despierta a todos los hilos para que terminen
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

// reparte bloques: entrega el siguiente índice libre bajo mutex, así cada bloque tiene un único dueño
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

// arma el árbol de Huffman uniendo siempre los dos nodos activos de menor frecuencia; devuelve la raíz
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

// recorre el árbol y asigna a cada símbolo su código (0 = izquierda, 1 = derecha)
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

// convierte un bloque en bits con los códigos de Huffman y los empaqueta en bytes
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

// trabajador de la fase 1: cuenta frecuencias en un arreglo local y lo suma al global al final
static void *hilo_conteo(void *arg) {
    TrabajoCompresion *t = arg;
    // conteo local sin lock; se suma al global una sola vez al final
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

// trabajador de la fase 3: toma bloques respetando la ventana, los codifica y los deja listos
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
        // contrapresión: no se adelanta más de 'ventana' bloques a lo escrito
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

// lanza num_hilos trabajadores con la rutina dada y espera a que todos terminen
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

// consumidor: espera cada bloque en orden, lo escribe y avisa que liberó espacio en la ventana
static void escribir_bloques_en_orden(TrabajoCompresion *t) {
    for (size_t i = 0; i < t->num_bloques; i++) {
        pthread_mutex_lock(&t->mutex);
        // duerme hasta que el bloque i esté listo: la salida queda en orden
        while (!t->abortar && !t->bloques[i].listo) {
            pthread_cond_wait(&t->cond_bloque_listo, &t->mutex);
        }
        if (t->abortar) {
            pthread_mutex_unlock(&t->mutex);
            return;
        }
        // se copia el bloque y se escribe fuera del mutex
        BloqueComprimido b = t->bloques[i];
        t->bloques[i].datos = NULL;
        pthread_mutex_unlock(&t->mutex);

        int fallo;
        uint64_t escritos_bytes;
        if (t->descomprimir) {
            fallo = escribir_todo(t->fd_salida, b.datos, b.tam_bytes) != 0;
            escritos_bytes = b.tam_bytes;
        } else {
            fallo = escribir_todo(t->fd_salida, &b.num_bits, sizeof(b.num_bits)) != 0 ||
                    escribir_todo(t->fd_salida, b.datos, b.tam_bytes) != 0;
            escritos_bytes = sizeof(b.num_bits) + b.tam_bytes;
        }
        free(b.datos);
        if (fallo) {
            marcar_error(t, strerror(errno));
            return;
        }

        pthread_mutex_lock(&t->mutex);
        t->bloques_escritos++;
        t->bytes_salida += escritos_bytes;
        pthread_cond_broadcast(&t->cond_espacio);
        pthread_mutex_unlock(&t->mutex);
    }
}

// cambia la fase del trabajo y reinicia el reparto de bloques
static void cambiar_fase(TrabajoCompresion *t, FaseCompresor fase) {
    pthread_mutex_lock(&t->mutex);
    t->fase = fase;
    t->siguiente_bloque = 0;
    pthread_mutex_unlock(&t->mutex);
}

// lee la bandera abortar bajo mutex
static int hay_que_abortar(TrabajoCompresion *t) {
    pthread_mutex_lock(&t->mutex);
    int abortar = t->abortar;
    pthread_mutex_unlock(&t->mutex);
    return abortar;
}

// lanza el pool de trabajadores y, mientras tanto, escribe los bloques en orden desde el coordinador
static void ejecutar_etapa_final(TrabajoCompresion *t, void *(*rutina)(void *)) {
    cambiar_fase(t, FASE_CODIFICACION);
    pthread_t hilos[t->num_hilos];
    int creados = 0;
    for (int h = 0; h < t->num_hilos; h++) {
        if (pthread_create(&hilos[h], NULL, rutina, t) != 0) {
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

// cierre único: libera bloques, cierra archivos, borra la salida si hubo error o cancelación y publica el resultado
static void finalizar_trabajo(TrabajoCompresion *t) {
    for (size_t i = 0; i < t->num_bloques; i++) {
        free(t->bloques[i].datos);
        t->bloques[i].datos = NULL;
    }
    close(t->fd_entrada);
    if (close(t->fd_salida) != 0) {
        marcar_error(t, strerror(errno));
    }

    pthread_mutex_lock(&t->mutex);
    int error = t->abortar;
    int cancelado = t->cancelado;
    pthread_mutex_unlock(&t->mutex);
    if (error) {
        unlink(t->ruta_salida);
    }

    pthread_mutex_lock(&t->mutex);
    clock_gettime(CLOCK_MONOTONIC, &t->fin);
    uint64_t bytes_salida = t->bytes_salida;
    pthread_mutex_unlock(&t->mutex);

    // fase y mensaje juntos: el monitor no repinta el progreso tras el cierre
    pthread_mutex_lock(&g_mutex_salida);
    pthread_mutex_lock(&t->mutex);
    t->fase = error ? FASE_ERROR : FASE_TERMINADO;
    pthread_mutex_unlock(&t->mutex);

    if (!cancelado) {
        printf(g_esperando_entrada ? "\r\033[K" : "\n");
        if (error) {
            printf(COLOR_ERROR "[compresor] Error al %s '%s': %s\n" COLOR_RESET,
                   t->descomprimir ? "descomprimir" : "comprimir", t->ruta_entrada, t->mensaje_error);
        } else if (t->descomprimir) {
            printf(COLOR_RESULT "[compresor] Terminado: %s -> %s (%llu -> %llu bytes) en %.3f s\n" COLOR_RESET,
                   t->ruta_entrada, t->ruta_salida,
                   (unsigned long long)t->tam_entrada, (unsigned long long)bytes_salida,
                   segundos_desde(&t->inicio, &t->fin));
        } else {
            double ratio = t->tam_archivo > 0 ? 100.0 * (double)bytes_salida / (double)t->tam_archivo : 0.0;
            printf(COLOR_RESULT "[compresor] Terminado: %s -> %s (%llu -> %llu bytes, %.1f%%) en %.3f s\n" COLOR_RESET,
                   t->ruta_entrada, t->ruta_salida,
                   (unsigned long long)t->tam_archivo, (unsigned long long)bytes_salida, ratio,
                   segundos_desde(&t->inicio, &t->fin));
        }
        if (g_esperando_entrada) {
            imprimir_prompt_texto(NULL);
            g_prompt_con_progreso = 0;
        }
        fflush(stdout);
    }
    pthread_mutex_unlock(&g_mutex_salida);
}

// decodifica un bloque recorriendo el árbol (bit 0 = izquierda, 1 = derecha, hoja = símbolo); devuelve 0 si produjo exactamente los bytes esperados
static int decodificar_bloque(const TrabajoCompresion *t, const unsigned char *entrada, uint64_t num_bits,
                              unsigned char *salida, size_t esperado) {
    const NodoHuffman *nodos = t->nodos;
    int hoja_unica = nodos[t->raiz].izq == -1;
    int nodo = t->raiz;
    size_t producidos = 0;

    for (uint64_t pos = 0; pos < num_bits; pos++) {
        if (producidos >= esperado) return -1;
        if (hoja_unica) {
            salida[producidos++] = (unsigned char)nodos[t->raiz].simbolo;
            continue;
        }
        int bit = (entrada[pos >> 3] >> (7 - (pos & 7))) & 1;
        nodo = bit ? nodos[nodo].der : nodos[nodo].izq;
        if (nodos[nodo].izq == -1) {
            salida[producidos++] = (unsigned char)nodos[nodo].simbolo;
            nodo = t->raiz;
        }
    }
    return (producidos == esperado && nodo == t->raiz) ? 0 : -1;
}

// marca el trabajo como fallido indicando el bloque con datos inválidos
static void marcar_bloque_corrupto(TrabajoCompresion *t, size_t i) {
    char mensaje[sizeof(t->mensaje_error)];
    snprintf(mensaje, sizeof(mensaje), "archivo .huf corrupto (bloque %zu)", i);
    marcar_error(t, mensaje);
}

// trabajador de descompresión: lee un bloque comprimido, lo decodifica y lo deja listo para el escritor
static void *hilo_decodificacion(void *arg) {
    TrabajoCompresion *t = arg;

    size_t i;
    while (tomar_siguiente_bloque(t, &i)) {
        pthread_mutex_lock(&t->mutex);
        // contrapresión: no se adelanta más de 'ventana' bloques a lo escrito
        while (!t->abortar && i >= t->bloques_escritos + t->ventana) {
            pthread_cond_wait(&t->cond_espacio, &t->mutex);
        }
        int abortar = t->abortar;
        pthread_mutex_unlock(&t->mutex);
        if (abortar) break;

        size_t esperado = tam_de_bloque(t, i);
        size_t tam_entrada = t->bloques[i].tam_bytes;
        unsigned char *entrada = malloc(tam_entrada > 0 ? tam_entrada : 1);
        unsigned char *salida = malloc(esperado > 0 ? esperado : 1);
        if (entrada == NULL || salida == NULL) {
            free(entrada);
            free(salida);
            marcar_error(t, "malloc: sin memoria para el bloque");
            break;
        }

        ssize_t n = leer_en(t->fd_entrada, entrada, tam_entrada, t->bloques[i].offset);
        if (n < 0 || (size_t)n != tam_entrada) {
            free(entrada);
            free(salida);
            if (n < 0) marcar_error(t, strerror(errno));
            else marcar_bloque_corrupto(t, i);
            break;
        }

        int rc = decodificar_bloque(t, entrada, t->bloques[i].num_bits, salida, esperado);
        free(entrada);
        if (rc != 0) {
            free(salida);
            marcar_bloque_corrupto(t, i);
            break;
        }

        pthread_mutex_lock(&t->mutex);
        t->bloques[i].datos = salida;
        t->bloques[i].tam_bytes = esperado;
        t->bloques[i].listo = 1;
        t->bloques_codificados++;
        pthread_cond_signal(&t->cond_bloque_listo);
        pthread_mutex_unlock(&t->mutex);
    }
    return NULL;
}

// los bloques miden distinto: lee los prefijos num_bits para ubicar dónde empieza cada uno
static void indexar_bloques(TrabajoCompresion *t) {
    uint64_t pos = sizeof(CabeceraHuf);
    for (size_t i = 0; i < t->num_bloques; i++) {
        if (hay_que_abortar(t)) return;

        uint64_t num_bits;
        ssize_t n = leer_en(t->fd_entrada, &num_bits, sizeof(num_bits), pos);
        if (n < 0) {
            marcar_error(t, strerror(errno));
            return;
        }
        if ((size_t)n != sizeof(num_bits) || num_bits > (uint64_t)tam_de_bloque(t, i) * MAX_LONG_CODIGO) {
            marcar_bloque_corrupto(t, i);
            return;
        }

        uint64_t tam_bytes = (num_bits + 7) / 8;
        if (pos + sizeof(num_bits) + tam_bytes > t->tam_entrada) {
            marcar_bloque_corrupto(t, i);
            return;
        }
        t->bloques[i].num_bits = num_bits;
        t->bloques[i].tam_bytes = (size_t)tam_bytes;
        t->bloques[i].offset = pos + sizeof(num_bits);
        pos += sizeof(num_bits) + tam_bytes;
    }
    if (pos != t->tam_entrada) {
        marcar_error(t, "archivo .huf corrupto (bytes sobrantes al final)");
    }
}

// hilo de fondo de des: indexa los bloques, decodifica en paralelo y cierra el trabajo
static void *hilo_coordinador_descompresion(void *arg) {
    TrabajoCompresion *t = arg;

    indexar_bloques(t);

    if (!hay_que_abortar(t) && t->num_bloques > 0) {
        ejecutar_etapa_final(t, hilo_decodificacion);
    }

    finalizar_trabajo(t);
    return NULL;
}

// hilo de fondo de comp: conteo, árbol, cabecera, codificación en paralelo y cierre
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
        memcpy(cab.firma, FIRMA_HUF, sizeof(cab.firma));
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
        ejecutar_etapa_final(t, hilo_codificacion);
    }

    finalizar_trabajo(t);
    return NULL;
}

// destruye el mutex y las condiciones y libera la memoria del trabajo
static void liberar_trabajo(TrabajoCompresion *t) {
    pthread_mutex_destroy(&t->mutex);
    pthread_cond_destroy(&t->cond_bloque_listo);
    pthread_cond_destroy(&t->cond_espacio);
    free(t->bloques);
    free(t);
}

// devuelve 1 si hay un trabajo que aún no terminó ni falló
static int trabajo_en_curso(void) {
    if (g_trabajo == NULL) return 0;

    pthread_mutex_lock(&g_trabajo->mutex);
    FaseCompresor fase = g_trabajo->fase;
    pthread_mutex_unlock(&g_trabajo->mutex);
    return fase != FASE_TERMINADO && fase != FASE_ERROR;
}

// si el trabajo anterior acabó, hace join y lo libera; devuelve 1 si sigue en curso
static int recoger_trabajo_terminado(void) {
    if (g_trabajo == NULL) return 0;
    if (trabajo_en_curso()) return 1;

    pthread_join(g_trabajo->hilo_coordinador, NULL);
    liberar_trabajo(g_trabajo);
    g_trabajo = NULL;
    return 0;
}

// guarda dev e ino de la entrada y la salida; identifican el archivo aunque cambie la ruta
static int registrar_identidad(TrabajoCompresion *t) {
    struct stat st_in, st_out;
    if (fstat(t->fd_entrada, &st_in) == -1 || fstat(t->fd_salida, &st_out) == -1) {
        printf(COLOR_ERROR "fstat: %s\n" COLOR_RESET, strerror(errno));
        return -1;
    }
    t->dev_entrada = st_in.st_dev;
    t->ino_entrada = st_in.st_ino;
    t->dev_salida = st_out.st_dev;
    t->ino_salida = st_out.st_ino;
    return 0;
}

// compara un dev/ino con los del trabajo en curso (entrada o salida) bajo mutex
static int identidad_en_uso(dev_t dev, ino_t ino) {
    int en_uso = 0;
    pthread_mutex_lock(&g_mutex_trabajo);
    if (trabajo_en_curso()) {
        const TrabajoCompresion *t = g_trabajo;
        en_uso = (t->dev_entrada == dev && t->ino_entrada == ino) ||
                 (t->dev_salida == dev && t->ino_salida == ino);
    }
    pthread_mutex_unlock(&g_mutex_trabajo);
    return en_uso;
}

// devuelve 1 si el descriptor apunta a un archivo de un trabajo en curso (lo usa el editor)
int compresor_fd_en_uso(int fd) {
    struct stat st;
    if (fstat(fd, &st) == -1) return 0;
    return identidad_en_uso(st.st_dev, st.st_ino);
}

// igual que compresor_fd_en_uso pero por ruta (lo usan d_create y d_copy)
int compresor_ruta_en_uso(const char *ruta) {
    struct stat st;
    if (stat(ruta, &st) == -1) return 0;
    return identidad_en_uso(st.st_dev, st.st_ino);
}

// al salir de la shell: detiene el monitor y espera a que termine el trabajo en curso
void compresor_finalizar(void) {
    monitor_detener();
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

// comando comp: valida el archivo, prepara el trabajo y lanza el coordinador en segundo plano
int cmd_c_comprimir(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: comp <archivo>\n" COLOR_RESET);
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

    if (registrar_identidad(t) != 0) {
        unlink(t->ruta_salida);
        goto error;
    }

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

// lee la cabecera completa del .huf; 0 si pudo, -1 si hubo error, -2 si el archivo termina antes
static int leer_cabecera(int fd, CabeceraHuf *cab) {
    unsigned char *p = (unsigned char *)cab;
    size_t leidos = 0;
    while (leidos < sizeof(*cab)) {
        ssize_t n = read(fd, p + leidos, sizeof(*cab) - leidos);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -2;
        leidos += (size_t)n;
    }
    return 0;
}

// comprueba firma, tamaños, número de bloques y suma de frecuencias antes de reservar memoria
static int cabecera_valida(const CabeceraHuf *cab, uint64_t tam_entrada) {
    if (memcmp(cab->firma, FIRMA_HUF, sizeof(cab->firma)) != 0) return -1;
    if (cab->tam_bloque == 0 || cab->tam_bloque > MAX_TAM_BLOQUE) return -1;
    if (cab->tam_original > UINT64_MAX - cab->tam_bloque) return -1;

    uint64_t esperados = (cab->tam_original + cab->tam_bloque - 1) / cab->tam_bloque;
    if (esperados != cab->num_bloques) return -1;
    if ((uint64_t)cab->num_bloques * sizeof(uint64_t) > tam_entrada - sizeof(CabeceraHuf)) return -1;

    uint64_t suma = 0;
    for (int s = 0; s < NUM_SIMBOLOS; s++) {
        if (cab->frecuencias[s] > cab->tam_original - suma) return -1;
        suma += cab->frecuencias[s];
    }
    return suma == cab->tam_original ? 0 : -1;
}

// comando des: valida la cabecera, reconstruye el árbol y lanza la descompresión en segundo plano (sin salida usa <nombre>.out)
int cmd_c_descomprimir(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, COLOR_ERROR "Uso: des <archivo.huf> [salida]\n" COLOR_RESET);
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
    t->descomprimir = 1;
    snprintf(t->ruta_entrada, sizeof(t->ruta_entrada), "%s", archivo);

    int truncada;
    if (argc == 3) {
        truncada = snprintf(t->ruta_salida, sizeof(t->ruta_salida), "%s", argv[2]) >= (int)sizeof(t->ruta_salida);
    } else {
        size_t len = strlen(archivo);
        if (len > 4 && strcmp(archivo + len - 4, ".huf") == 0) len -= 4;
        truncada = snprintf(t->ruta_salida, sizeof(t->ruta_salida), "%.*s.out", (int)len, archivo) >=
                   (int)sizeof(t->ruta_salida);
    }
    if (truncada) {
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

    struct stat st_salida;
    if (stat(t->ruta_salida, &st_salida) == 0 && st_salida.st_dev == st.st_dev && st_salida.st_ino == st.st_ino) {
        printf(COLOR_ERROR "[compresor] La salida no puede ser el mismo archivo de entrada.\n" COLOR_RESET);
        goto error;
    }

    t->tam_entrada = (uint64_t)st.st_size;
    CabeceraHuf cab;
    if (t->tam_entrada < sizeof(cab)) {
        printf(COLOR_ERROR "[compresor] '%s' no es un archivo .huf válido (demasiado pequeño).\n" COLOR_RESET, archivo);
        goto error;
    }
    LOG_SYSCALL("read", "%d, &cabecera, %zu", t->fd_entrada, sizeof(cab));
    if (leer_cabecera(t->fd_entrada, &cab) != 0) {
        LOG_SYSCALL_ERROR(strerror(errno));
        goto error;
    }
    LOG_SYSCALL_RESULT(sizeof(cab));
    if (cabecera_valida(&cab, t->tam_entrada) != 0) {
        printf(COLOR_ERROR "[compresor] '%s' no es un archivo .huf válido (cabecera corrupta).\n" COLOR_RESET, archivo);
        goto error;
    }

    t->tam_archivo = cab.tam_original;
    t->tam_bloque = cab.tam_bloque;
    t->num_bloques = cab.num_bloques;
    memcpy(t->frecuencias, cab.frecuencias, sizeof(t->frecuencias));
    t->raiz = -1;
    if (t->tam_archivo > 0) {
        t->raiz = construir_arbol(t->frecuencias, t->nodos);
        if (t->raiz < 0) {
            printf(COLOR_ERROR "[compresor] '%s' no es un archivo .huf válido (sin símbolos).\n" COLOR_RESET, archivo);
            goto error;
        }
    }

    t->num_hilos = g_num_hilos;
    if ((size_t)t->num_hilos > t->num_bloques) {
        t->num_hilos = t->num_bloques > 0 ? (int)t->num_bloques : 1;
    }
    t->ventana = 2 * (size_t)t->num_hilos;
    t->fase = FASE_ARBOL;

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

    if (registrar_identidad(t) != 0) {
        unlink(t->ruta_salida);
        goto error;
    }

    pthread_mutex_init(&t->mutex, NULL);
    pthread_cond_init(&t->cond_bloque_listo, NULL);
    pthread_cond_init(&t->cond_espacio, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t->inicio);

    LOG_SYSCALL("pthread_create", "coordinador");
    int rc = pthread_create(&t->hilo_coordinador, NULL, hilo_coordinador_descompresion, t);
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

    printf(COLOR_INFO "[compresor] Descompresión iniciada: %s -> %s (%zu bloques de %zu bytes, %d hilos)\n" COLOR_RESET,
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

// texto de la fase actual, distinto para compresión y descompresión
static const char *nombre_fase(const TrabajoCompresion *t, FaseCompresor fase) {
    if (t->descomprimir) {
        switch (fase) {
            case FASE_ARBOL:        return "1/2 lectura del índice de bloques";
            case FASE_CODIFICACION: return "2/2 decodificación y escritura";
            default: break;
        }
    }
    switch (fase) {
        case FASE_CONTEO:       return "1/3 conteo de frecuencias";
        case FASE_ARBOL:        return "2/3 construcción del árbol";
        case FASE_CODIFICACION: return "3/3 codificación y escritura";
        case FASE_TERMINADO:    return "terminado";
        case FASE_ERROR:        return "error";
    }
    return "desconocida";
}

// porcentaje de avance según el modo (comprimir o descomprimir) y la fase
static double calcular_porcentaje(const TrabajoCompresion *t, FaseCompresor fase, size_t contados, size_t escritos) {
    size_t n = t->num_bloques;
    if (fase == FASE_TERMINADO || n == 0) return 100.0;
    if (t->descomprimir) return fase == FASE_ARBOL ? 0.0 : 100.0 * (double)escritos / (double)n;
    if (fase == FASE_CONTEO) return 50.0 * (double)contados / (double)n;
    return 50.0 + 50.0 * (double)escritos / (double)n;
}

// texto de ancho fijo con el avance, p. ej. comprimiendo  45%; devuelve 0 si no hay trabajo en curso
static int resumen_trabajo(char *buf, size_t tam) {
    int hay = 0;
    pthread_mutex_lock(&g_mutex_trabajo);
    if (trabajo_en_curso()) {
        TrabajoCompresion *t = g_trabajo;
        pthread_mutex_lock(&t->mutex);
        FaseCompresor fase = t->fase;
        size_t contados = t->bloques_contados;
        size_t escritos = t->bloques_escritos;
        pthread_mutex_unlock(&t->mutex);
        snprintf(buf, tam, "%s %3.0f%%", t->descomprimir ? "descomprimiendo" : "comprimiendo",
                 calcular_porcentaje(t, fase, contados, escritos));
        hay = 1;
    }
    pthread_mutex_unlock(&g_mutex_trabajo);
    return hay;
}

// imprime el prompt (con progreso si hay trabajo) y marca que la shell queda esperando entrada
void compresor_prompt_imprimir(void) {
    char resumen[64];
    pthread_mutex_lock(&g_mutex_salida);
    int hay = resumen_trabajo(resumen, sizeof(resumen));
    imprimir_prompt_texto(hay ? resumen : NULL);
    fflush(stdout);
    g_prompt_con_progreso = hay;
    g_esperando_entrada = 1;
    pthread_mutex_unlock(&g_mutex_salida);
}

// la shell ya recibió la línea: el monitor deja de redibujar
void compresor_entrada_recibida(void) {
    pthread_mutex_lock(&g_mutex_salida);
    g_esperando_entrada = 0;
    g_prompt_con_progreso = 0;
    pthread_mutex_unlock(&g_mutex_salida);
}

#define INTERVALO_MONITOR_MS 200

// cada 200 ms repinta el progreso en el prompt si la shell espera entrada
static void *hilo_monitor(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_mutex_salida);
    while (!g_monitor_detener) {
        struct timespec limite;
        clock_gettime(CLOCK_REALTIME, &limite);
        limite.tv_nsec += INTERVALO_MONITOR_MS * 1000000L;
        if (limite.tv_nsec >= 1000000000L) {
            limite.tv_sec++;
            limite.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&g_cond_monitor, &g_mutex_salida, &limite);
        if (g_monitor_detener) break;
        if (!g_esperando_entrada || !g_prompt_con_progreso) continue;

        char resumen[64];
        // se suelta g_mutex_salida para no interbloquearse con stop (que hace join)
        pthread_mutex_unlock(&g_mutex_salida);
        int hay = resumen_trabajo(resumen, sizeof(resumen));
        pthread_mutex_lock(&g_mutex_salida);

        if (hay && g_esperando_entrada && g_prompt_con_progreso && !g_monitor_detener) {
            // guarda el cursor y repinta solo el prompt; el texto del usuario no se toca
            printf("\033" "7\r");
            imprimir_prompt_texto(resumen);
            // restaura el cursor donde el usuario seguía escribiendo
            printf("\033" "8");
            fflush(stdout);
        }
    }
    pthread_mutex_unlock(&g_mutex_salida);
    return NULL;
}

// crea el hilo monitor 
void compresor_monitor_iniciar(void) {
    if (g_monitor_activo || !isatty(STDOUT_FILENO)) return;
    if (pthread_create(&g_hilo_monitor, NULL, hilo_monitor, NULL) == 0) {
        g_monitor_activo = 1;
    }
}

// pide al monitor que termine y espera con join, util al cerrar la shell
static void monitor_detener(void) {
    if (!g_monitor_activo) return;
    pthread_mutex_lock(&g_mutex_salida);
    g_monitor_detener = 1;
    pthread_cond_signal(&g_cond_monitor);
    pthread_mutex_unlock(&g_mutex_salida);
    pthread_join(g_hilo_monitor, NULL);
    g_monitor_activo = 0;
}

// dibuja la barra de progreso
static void imprimir_barra(double porcentaje) {
    int llenos = (int)(porcentaje / 100.0 * ANCHO_BARRA);
    printf("  Progreso:  [" COLOR_RESULT);
    for (int i = 0; i < ANCHO_BARRA; i++) {
        putchar(i < llenos ? '#' : '-');
    }
    printf(COLOR_RESET "] %5.1f%%\n", porcentaje);
}

// comando es: muestra fase, barra, bloques, tamaños y tiempo del trabajo actual o del último
int cmd_c_estado(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, COLOR_ERROR "Uso: es\n" COLOR_RESET);
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

    size_t n = t->num_bloques;
    double porcentaje = calcular_porcentaje(t, fase, contados, escritos);

    printf(COLOR_TITLE "\n--- Estado del compresor ---\n" COLOR_RESET);
    printf("  Operación: " COLOR_PARAM "%s" COLOR_RESET "\n", t->descomprimir ? "descompresión" : "compresión");
    printf("  Archivo:   %s -> %s\n", t->ruta_entrada, t->ruta_salida);
    printf("  Fase:      " COLOR_PARAM "%s" COLOR_RESET "\n", nombre_fase(t, fase));
    if (fase != FASE_ERROR) {
        imprimir_barra(porcentaje);
    }
    if (t->descomprimir) {
        printf("  Bloques:   decodificados %zu/%zu, escritos %zu/%zu\n", codificados, n, escritos, n);
    } else {
        printf("  Bloques:   contados %zu/%zu, codificados %zu/%zu, escritos %zu/%zu\n",
               contados, n, codificados, n, escritos, n);
    }
    printf("  Config:    %d hilos, bloques de %zu bytes\n", t->num_hilos, t->tam_bloque);
    printf("  Tamaño:    %llu -> %llu bytes\n",
           (unsigned long long)(t->descomprimir ? t->tam_entrada : t->tam_archivo),
           (unsigned long long)bytes_salida);
    printf("  Tiempo:    %.3f s\n", transcurrido);
    if (fase == FASE_ERROR) {
        printf("  " COLOR_ERROR "Error:     %s" COLOR_RESET "\n", mensaje_error);
    }

    pthread_mutex_unlock(&g_mutex_trabajo);
    return 0;
}

// comando stop: aborta el trabajo en curso, espera su cierre y elimina la salida parcial
int cmd_c_cancelar(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, COLOR_ERROR "Uso: stop\n" COLOR_RESET);
        return 1;
    }

    pthread_mutex_lock(&g_mutex_trabajo);
    if (!trabajo_en_curso()) {
        pthread_mutex_unlock(&g_mutex_trabajo);
        printf(COLOR_INFO "[compresor] No hay ningún trabajo en curso que cancelar.\n" COLOR_RESET);
        return 1;
    }

    TrabajoCompresion *t = g_trabajo;
    pthread_mutex_lock(&t->mutex);
    t->cancelado = 1;
    pthread_mutex_unlock(&t->mutex);
    // despierta a todos los hilos y espera su cierre limpio con join
    marcar_error(t, MENSAJE_CANCELADO);

    LOG_SYSCALL("pthread_join", "coordinador");
    int rc = pthread_join(t->hilo_coordinador, NULL);
    if (rc != 0) {
        LOG_SYSCALL_ERROR(strerror(rc));
    } else {
        LOG_SYSCALL_RESULT(0);
    }

    FaseCompresor fase = t->fase;
    int cancelado = strcmp(t->mensaje_error, MENSAJE_CANCELADO) == 0;
    char ruta_entrada[PATH_MAX];
    char mensaje_error[sizeof(t->mensaje_error)];
    snprintf(ruta_entrada, sizeof(ruta_entrada), "%s", t->ruta_entrada);
    snprintf(mensaje_error, sizeof(mensaje_error), "%s", t->mensaje_error);

    liberar_trabajo(t);
    g_trabajo = NULL;
    pthread_mutex_unlock(&g_mutex_trabajo);

    if (fase == FASE_TERMINADO) {
        printf(COLOR_INFO "[compresor] El trabajo '%s' terminó justo antes de cancelarse; la salida se conserva.\n" COLOR_RESET,
               ruta_entrada);
    } else if (cancelado) {
        printf(COLOR_RESULT "[compresor] Trabajo '%s' cancelado; salida parcial eliminada.\n" COLOR_RESET, ruta_entrada);
    } else {
        printf(COLOR_ERROR "[compresor] El trabajo '%s' ya había fallado: %s\n" COLOR_RESET, ruta_entrada, mensaje_error);
    }
    return 0;
}

// lee el archivo por trozos y calcula su FNV-1a de 64 bits y su tamaño
static int checksum_archivo(const char *ruta, uint64_t *checksum, uint64_t *tamano) {
    LOG_SYSCALL("open", "\"%s\", O_RDONLY", ruta);
    int fd = open(ruta, O_RDONLY);
    if (fd == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return -1;
    }
    LOG_SYSCALL_RESULT(fd);

    unsigned char buf[64 * 1024];
    uint64_t hash = 14695981039346656037ULL;
    uint64_t total = 0;
    int fallo = 0;

    LOG_SYSCALL("read", "%d, buf, %zu", fd, sizeof(buf));
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            fallo = 1;
            break;
        }
        if (n == 0) break;
        for (ssize_t k = 0; k < n; k++) {
            // FNV-1a: XOR del byte y multiplicación por un primo; el orden importa
            hash ^= buf[k];
            hash *= 1099511628211ULL;
        }
        total += (uint64_t)n;
    }
    if (fallo) {
        LOG_SYSCALL_ERROR(strerror(errno));
    } else {
        LOG_SYSCALL_RESULT(total);
    }

    LOG_SYSCALL("close", "%d", fd);
    close(fd);
    LOG_SYSCALL_RESULT(0);

    if (fallo) return -1;
    *checksum = hash;
    *tamano = total;
    return 0;
}

// comando ver: compara el tamaño y el checksum de dos archivos
int cmd_c_verificar(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, COLOR_ERROR "Uso: ver <original> <descomprimido>\n" COLOR_RESET);
        return 1;
    }

    uint64_t hash_a, hash_b, tam_a, tam_b;
    if (checksum_archivo(argv[1], &hash_a, &tam_a) != 0) return 1;
    if (checksum_archivo(argv[2], &hash_b, &tam_b) != 0) return 1;

    printf(COLOR_TITLE "\n--- Verificación de integridad ---\n" COLOR_RESET);
    printf("  %-24s %12llu bytes  checksum " COLOR_PARAM "%016llx" COLOR_RESET "\n",
           argv[1], (unsigned long long)tam_a, (unsigned long long)hash_a);
    printf("  %-24s %12llu bytes  checksum " COLOR_PARAM "%016llx" COLOR_RESET "\n",
           argv[2], (unsigned long long)tam_b, (unsigned long long)hash_b);

    if (tam_a == tam_b && hash_a == hash_b) {
        printf(COLOR_RESULT "[compresor] Integridad verificada: los archivos son idénticos.\n" COLOR_RESET);
        return 0;
    }
    printf(COLOR_ERROR "[compresor] Los archivos son distintos.\n" COLOR_RESET);
    return 1;
}

// interpreta un tamaño con sufijo K o M
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

// comando config: muestra o cambia hilos y tamaño de bloque del próximo trabajo
int cmd_c_config(int argc, char **argv) {
    if (argc != 1 && argc != 3) {
        fprintf(stderr, COLOR_ERROR "Uso: config [<hilos> <tam_bloque>]  (tam_bloque en bytes, admite K o M)\n" COLOR_RESET);
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
