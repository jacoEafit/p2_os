#include "shell.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <errno.h>
#include <time.h>

/**
 * ====================================================================================
 * COMANDO: d_create <archivo> <texto>
 * ====================================================================================
 * Demuestra la creación de archivos a bajo nivel mediante llamadas al sistema.
 * 
 * Syscalls explicadas:
 * 1. open(2): Solicita al kernel abrir o crear un archivo.
 *    - O_WRONLY: Abre el archivo solo para escritura.
 *    - O_CREAT: Si el archivo no existe, lo crea.
 *    - O_TRUNC: Si el archivo existe, reduce su tamaño a 0 (lo vacía).
 *    - 0644 (modo): Permisos en octal (-rw-r--r--) aplicados si se crea el archivo.
 * 2. write(2): Escribe una secuencia de bytes en el descriptor de archivo (File Descriptor).
 * 3. close(2): Libera la entrada en la tabla de descriptores de archivos del proceso.
 */
int cmd_d_create(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, COLOR_ERROR "Uso: d_create <archivo> <texto>\n" COLOR_RESET);
        return 1;
    }
    const char *filename = argv[1];
    const char *text = argv[2];
    size_t len = strlen(text);

    /* 1. LLAMADA AL SISTEMA: open */
    LOG_SYSCALL("open", "\"%s\", O_WRONLY|O_CREAT|O_TRUNC, 0644", filename);
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        /* Si falla open, retorna -1 y asigna el código en errno */
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(fd); /* El valor devuelto es un entero llamado descriptor de archivo (File Descriptor) */

    /* 2. LLAMADA AL SISTEMA: write */
    LOG_SYSCALL("write", "%d, \"%s\", %zu", fd, text, len);
    ssize_t bytes_written = write(fd, text, len);
    if (bytes_written == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        close(fd); /* Cerrar el descriptor para evitar fuga de recursos en caso de error */
        return 1;
    }
    LOG_SYSCALL_RESULT(bytes_written); /* Retorna la cantidad real de bytes escritos en el disco */

    /* 3. LLAMADA AL SISTEMA: close */
    LOG_SYSCALL("close", "%d", fd);
    int res = close(fd);
    if (res == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(res); /* Retorna 0 en caso de éxito */

    printf(COLOR_RESULT "Archivo '%s' creado exitosamente con %zd bytes.\n" COLOR_RESET, filename, bytes_written);
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: d_read <archivo>
 * ====================================================================================
 * Demuestra la lectura de archivos utilizando descriptores de archivo del sistema.
 * 
 * Syscalls explicadas:
 * 1. open(2): Abre un archivo existente.
 *    - O_RDONLY: Abre el archivo en modo de solo lectura.
 * 2. read(2): Copia bytes desde el archivo apuntado por el descriptor al buffer en memoria RAM.
 *    - Retorna el número de bytes leídos. Si llega al final del archivo (EOF), retorna 0.
 * 3. close(2): Cierra la conexión al archivo.
 */
int cmd_d_read(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: d_read <archivo>\n" COLOR_RESET);
        return 1;
    }
    const char *filename = argv[1];
    char buffer[1024];

    /* 1. LLAMADA AL SISTEMA: open */
    LOG_SYSCALL("open", "\"%s\", O_RDONLY", filename);
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(fd);

    /* 2. LLAMADA AL SISTEMA: read */
    LOG_SYSCALL("read", "%d, buffer, %zu", fd, sizeof(buffer) - 1);
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        close(fd);
        return 1;
    }
    LOG_SYSCALL_RESULT(bytes_read);
    buffer[bytes_read] = '\0'; /* Añadir carácter nulo para poder imprimirlo de forma segura como string */

    /* 3. LLAMADA AL SISTEMA: close */
    LOG_SYSCALL("close", "%d", fd);
    int res = close(fd);
    if (res == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(res);

    printf(COLOR_TITLE "--- Contenido del Archivo ---\n" COLOR_RESET);
    printf("%s\n", buffer);
    printf(COLOR_TITLE "-----------------------------\n" COLOR_RESET);

    return 0;
}

/**
 * ====================================================================================
 * COMANDO: d_info <archivo>
 * ====================================================================================
 * Muestra información estructural del inodo de un archivo.
 * 
 * Syscalls explicadas:
 * 1. stat(2): Obtiene estadísticas de metadatos de un archivo sin necesidad de abrirlo.
 *    - Llena la estructura struct stat con metadatos del inodo del archivo en el sistema de ficheros.
 */
int cmd_d_info(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: d_info <archivo>\n" COLOR_RESET);
        return 1;
    }
    const char *filename = argv[1];
    struct stat st;

    /* 1. LLAMADA AL SISTEMA: stat */
    LOG_SYSCALL("stat", "\"%s\", &st", filename);
    int res = stat(filename, &st);
    if (res == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(res); /* Retorna 0 en caso de éxito */

    printf(COLOR_TITLE "--- Metadatos del Archivo (stat) ---\n" COLOR_RESET);
    /* st_dev: ID del dispositivo físico que contiene el archivo */
    printf("  Dispositivo:    [%ld,%ld]\n", (long)major(st.st_dev), (long)minor(st.st_dev));
    /* st_ino: Número de inodo único del archivo dentro del dispositivo */
    printf("  Inodo:          " COLOR_PARAM "%ld" COLOR_RESET "\n", (long)st.st_ino);
    /* st_mode: Modos y permisos de acceso del archivo */
    printf("  Permisos (oct): " COLOR_PARAM "%o" COLOR_RESET "\n", st.st_mode & 0777);
    /* st_nlink: Número de enlaces físicos apuntando a este inodo */
    printf("  Enlaces (nlink):%ld\n", (long)st.st_nlink);
    /* st_uid y st_gid: IDs de dueño y grupo asignados */
    printf("  UID de Dueño:   %ld\n", (long)st.st_uid);
    printf("  GID de Grupo:   %ld\n", (long)st.st_gid);
    /* st_size: Tamaño real del archivo en bytes */
    printf("  Tamaño:         " COLOR_RESULT "%ld bytes" COLOR_RESET "\n", (long)st.st_size);
    /* st_blksize: Tamaño óptimo de bloque sugerido por el sistema de ficheros para E/S */
    printf("  Tamaño Bloque:  %ld bytes\n", (long)st.st_blksize);
    /* st_blocks: Número real de bloques de 512 bytes asignados en el disco */
    printf("  Bloques Ocup.:  %ld\n", (long)st.st_blocks);
    /* st_mtime: Fecha y hora de la última modificación del contenido */
    printf("  MTime (Modif):  %s", ctime(&st.st_mtime));
    printf(COLOR_TITLE "------------------------------------\n" COLOR_RESET);

    return 0;
}

/**
 * ====================================================================================
 * COMANDO: d_copy <origen> <destino>
 * ====================================================================================
 * Demuestra la copia progresiva de datos a nivel de bytes mediante búferes.
 * 
 * Este comando abre el archivo de origen en modo lectura (O_RDONLY), el archivo destino
 * en modo escritura (O_WRONLY | O_CREAT | O_TRUNC), y transfiere los bloques leyéndolos 
 * en memoria intermedia RAM e inmediatamente escribiéndolos en el destino.
 */
int cmd_d_copy(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, COLOR_ERROR "Uso: d_copy <origen> <destino>\n" COLOR_RESET);
        return 1;
    }
    const char *src_filename = argv[1];
    const char *dst_filename = argv[2];
    char buffer[512];
    ssize_t bytes_read, bytes_written;

    /* 1. Abrir archivo origen (Lectura) */
    LOG_SYSCALL("open", "\"%s\", O_RDONLY", src_filename);
    int fd_src = open(src_filename, O_RDONLY);
    if (fd_src == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(fd_src);

    /* 2. Abrir/Crear archivo destino (Escritura) */
    LOG_SYSCALL("open", "\"%s\", O_WRONLY|O_CREAT|O_TRUNC, 0644", dst_filename);
    int fd_dst = open(dst_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dst == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        close(fd_src);
        return 1;
    }
    LOG_SYSCALL_RESULT(fd_dst);

    /* 3. Bucle de Copia: Lee del origen y escribe en el destino bloque a bloque */
    size_t total_copied = 0;
    while (1) {
        /* Leer del origen */
        LOG_SYSCALL("read", "%d, buffer, %zu", fd_src, sizeof(buffer));
        bytes_read = read(fd_src, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            LOG_SYSCALL_ERROR(strerror(errno));
            break;
        }
        LOG_SYSCALL_RESULT(bytes_read);
        if (bytes_read == 0) break; /* Fin de Archivo (EOF) alcanzado */

        /* Escribir en el destino */
        LOG_SYSCALL("write", "%d, buffer, %zd", fd_dst, bytes_read);
        bytes_written = write(fd_dst, buffer, bytes_read);
        if (bytes_written < 0) {
            LOG_SYSCALL_ERROR(strerror(errno));
            break;
        }
        LOG_SYSCALL_RESULT(bytes_written);
        total_copied += bytes_written;
    }

    /* 4. Cerrar descriptores */
    LOG_SYSCALL("close", "%d", fd_src);
    int res = close(fd_src);
    if (res == -1) LOG_SYSCALL_ERROR(strerror(errno));
    else LOG_SYSCALL_RESULT(res);

    LOG_SYSCALL("close", "%d", fd_dst);
    res = close(fd_dst);
    if (res == -1) LOG_SYSCALL_ERROR(strerror(errno));
    else LOG_SYSCALL_RESULT(res);

    printf(COLOR_RESULT "Copiado finalizado. Total de bytes copiados: %zu\n" COLOR_RESET, total_copied);
    return 0;
}
