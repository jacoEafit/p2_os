/*
 * =====================================================================================
 * Comparación entre Comandos de Terminal (CLI), System Calls (POSIX) y Librerías de C
 * =====================================================================================
 * 
 * Este código interactúa con el sistema de archivos utilizando Llamadas al Sistema
 * (System Calls). Estas llamadas se comunican directamente con el Kernel del Sistema Operativo.
 * 
 * A continuación, se muestra la relación y comparación con las funciones de la librería
 * estándar de C y los comandos equivalentes de la terminal:
 * 
 * | Operación             | System Call (Kernel)      | Función Librería C    | Comando CLI        | Diferencia / Explicación |
 * |-----------------------|---------------------------|-----------------------|--------------------|--------------------------|
 * | Crear/Abrir Archivo   | open()                    | fopen()               | touch, cat >       | open() devuelve un file descriptor (int) directo del OS. fopen() maneja buffers en memoria de usuario y devuelve un FILE*. |
 * | Leer Archivo          | read()                    | fread(), fgets()      | cat, less          | read() lee bytes crudos sin formato. fread() permite lectura tipada y con buffering. |
 * | Escribir (Añadir)     | write() (con O_APPEND)    | fwrite(), fprintf()   | echo ".." >>, tee  | write() envía bytes directo al SO. fprintf() permite dar formato a la cadena antes de enviarla. |
 * | Actualizar (Vaciar)   | open() (con O_TRUNC)      | freopen()             | echo ".." >        | O_TRUNC le indica al SO que borre el contenido del archivo antes de escribir. |
 * | Eliminar Archivo      | unlink()                  | remove()              | rm                 | unlink() elimina la referencia del archivo (inodo). remove() de C simplemente invoca a unlink() internamente en sistemas UNIX. |
 * | Metadatos / Info      | stat()                    | N/A                   | stat, ls -l        | Permite consultar el inodo directamente para obtener tamaño, permisos, etc. |
 * | Crear Directorio      | mkdir()                   | N/A                   | mkdir              | Delega al SO la creación de la estructura del directorio. |
 * | Eliminar Directorio   | rmdir()                   | N/A                   | rmdir              | Elimina un directorio (el cual debe estar vacío). |
 * | Cambiar Permisos      | chmod()                   | N/A                   | chmod              | Modifica directamente los permisos octales de un inodo. |
 * 
 * NOTA: La librería estándar de C (stdio.h) está construida "por encima" de las System Calls.
 * Usar System Calls da más control (obligatorio para consultar inodos o cambiar permisos), 
 * pero usar la librería de C suele ser más amigable y eficiente para procesar datos (debido a 
 * sus buffers automáticos en memoria de usuario).
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>      // Para constantes como O_CREAT, O_RDONLY, O_WRONLY
#include <unistd.h>     // Para llamadas POSIX como read, write, close, unlink, rmdir
#include <string.h>     // Para manejo de cadenas como strcmp, strlen
#include <sys/stat.h>   // Para stat, mkdir, chmod, y constantes de permisos y tipos de archivo
#include <sys/types.h>  // Tipos de datos del sistema como mode_t, ssize_t

// Función auxiliar para imprimir cómo usar el programa
void print_usage(const char *prog_name) {
    printf("Uso:\n");
    printf("  %s create <archivo>\n", prog_name);
    printf("  %s write <archivo> <texto>\n", prog_name);
    printf("  %s read <archivo>\n", prog_name);
    printf("  %s delete|rm <archivo>\n", prog_name);
    printf("  %s update <archivo> <nuevo_texto>\n", prog_name);
    printf("  %s mkdir <directorio>\n", prog_name);
    printf("  %s rmdir <directorio>\n", prog_name);
    printf("  %s info <archivo/directorio>\n", prog_name);
    printf("  %s copy <origen> <destino>\n", prog_name);
    printf("  %s chmod <archivo> <modo_octal>\n", prog_name);
}

int main(int argc, char *argv[]) {
    // Verificamos que al menos se pase un comando y un objetivo (mínimo 3 argumentos)
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];
    const char *target = argv[2];

    // ==========================================
    // 1. CREAR UN ARCHIVO (System call: open)
    // ==========================================
    if (strcmp(command, "create") == 0) {
        // open() devuelve un file descriptor (entero) o -1 si falla.
        // O_CREAT: Si el archivo no existe, lo crea.
        // O_WRONLY: Lo abre en modo solo escritura.
        // O_TRUNC: Si el archivo ya existe y tiene datos, lo trunca (borra su contenido a longitud 0).
        // 0644 (modo octal): Permisos r/w para el dueño, solo r (lectura) para grupo y otros.
        int fd = open(target, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd == -1) {
            perror("Error al crear el archivo");
            return 1;
        }
        printf("Archivo '%s' creado exitosamente (fd: %d).\n", target, fd);
        
        // Siempre se debe cerrar el descriptor de archivo para liberar recursos del kernel.
        close(fd);
    } 
    // ==========================================
    // 2. ESCRIBIR EN UN ARCHIVO (System call: write)
    // ==========================================
    else if (strcmp(command, "write") == 0) {
        if (argc < 4) {
            printf("Error: Falta el texto a escribir.\n");
            return 1;
        }
        const char *text = argv[3];
        
        // O_APPEND asegura que los datos se agreguen al final del archivo sin sobrescribir lo existente.
        int fd = open(target, O_WRONLY | O_APPEND);
        if (fd == -1) {
            perror("Error al abrir el archivo para escribir");
            return 1;
        }
        
        // write() intenta escribir una cantidad de bytes (strlen(text)) en el descriptor fd.
        // Devuelve el número de bytes realmente escritos, o -1 en caso de error.
        ssize_t bytes_written = write(fd, text, strlen(text));
        if (bytes_written == -1) {
            perror("Error al escribir en el archivo");
            close(fd);
            return 1;
        }
        printf("Se escribieron %zd bytes en el archivo '%s'.\n", bytes_written, target);
        close(fd);
    } 
    // ==========================================
    // 3. LEER DE UN ARCHIVO (System call: read)
    // ==========================================
    else if (strcmp(command, "read") == 0) {
        // O_RDONLY: Abrir el archivo en modo de solo lectura.
        int fd = open(target, O_RDONLY);
        if (fd == -1) {
            perror("Error al abrir el archivo para leer");
            return 1;
        }
        
        char buffer[1024]; // Buffer temporal en memoria de usuario para guardar los datos leídos.
        ssize_t bytes_read;
        printf("Contenido de '%s':\n", target);
        
        // read() lee hasta (sizeof(buffer) - 1) bytes. Retorna el número de bytes leídos o 0 al llegar al EOF (Fin de Archivo).
        while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0'; // Agregamos caracter nulo al final para poder imprimirlo como cadena.
            printf("%s", buffer);
        }
        if (bytes_read == -1) {
            perror("Error al leer el archivo");
        }
        printf("\n");
        close(fd);
    } 
    // ==========================================
    // 4. ELIMINAR UN ARCHIVO (System call: unlink)
    // ==========================================
    else if (strcmp(command, "delete") == 0 || strcmp(command, "rm") == 0) {
        // unlink() remueve el nombre del sistema de archivos. Si es el último enlace (link duro)
        // y ningún proceso lo tiene abierto, los bloques de datos se liberan (se elimina físicamente).
        if (unlink(target) == -1) {
            perror("Error al eliminar el archivo");
            return 1;
        }
        printf("Archivo '%s' eliminado exitosamente.\n", target);
    } 
    // ==========================================
    // 5. CREAR UN DIRECTORIO (System call: mkdir)
    // ==========================================
    else if (strcmp(command, "mkdir") == 0) {
        // mkdir() crea un directorio. El segundo parámetro (0755) son los permisos octales por defecto.
        if (mkdir(target, 0755) == -1) {
            perror("Error al crear el directorio");
            return 1;
        }
        printf("Directorio '%s' creado exitosamente.\n", target);
    } 
    // ==========================================
    // 6. ELIMINAR UN DIRECTORIO (System call: rmdir)
    // ==========================================
    else if (strcmp(command, "rmdir") == 0) {
        // rmdir() elimina un directorio. Nota: El directorio debe estar vacío.
        if (rmdir(target) == -1) {
            perror("Error al eliminar el directorio");
            return 1;
        }
        printf("Directorio '%s' eliminado exitosamente.\n", target);
    } 
    // ==========================================
    // 7. OBTENER INFORMACIÓN DE ARCHIVO (System call: stat)
    // ==========================================
    else if (strcmp(command, "info") == 0) {
        struct stat st; // Estructura provista por el kernel donde stat guardará los metadatos.
        
        // stat() lee los metadatos del archivo y los guarda en la variable 'st'.
        if (stat(target, &st) == -1) {
            perror("Error al obtener información");
            return 1;
        }
        printf("Información de '%s':\n", target);
        printf("  Tamaño: %ld bytes\n", st.st_size); // Tamaño real del archivo en bytes
        printf("  Inodo: %ld\n", st.st_ino);         // Número de Inodo (identificador único interno en disco)
        printf("  Modos/Permisos: %o\n", st.st_mode & 0777); // Filtramos con máscara para mostrar permisos limpios (ej: 644, 755)
        
        // Uso de macros del sistema (S_ISDIR y S_ISREG) para descifrar el tipo de archivo desde st_mode
        if (S_ISDIR(st.st_mode)) {
            printf("  Tipo: Directorio\n");
        } else if (S_ISREG(st.st_mode)) {
            printf("  Tipo: Archivo regular\n");
        }
    } 
    // ==========================================
    // 8. COPIAR UN ARCHIVO (System calls: open, read, write)
    // ==========================================
    else if (strcmp(command, "copy") == 0) {
        if (argc < 4) {
            printf("Error: Falta el archivo de destino.\n");
            return 1;
        }
        const char *dest = argv[3];
        
        // Abrimos origen para solo lectura
        int fd_src = open(target, O_RDONLY);
        if (fd_src == -1) {
            perror("Error al abrir el archivo origen");
            return 1;
        }
        
        // Creamos/abrimos destino para escritura, si no existe lo crea (O_CREAT), si existe lo vacía (O_TRUNC)
        int fd_dest = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_dest == -1) {
            perror("Error al abrir/crear el archivo destino");
            close(fd_src);
            return 1;
        }
        
        char buffer[4096]; // Buffer generoso (4KB, tamaño de bloque común en discos) para eficiencia
        ssize_t bytes_read, bytes_written;
        
        // Bucle clásico de copia: leemos en bloques y escribimos lo leído.
        while ((bytes_read = read(fd_src, buffer, sizeof(buffer))) > 0) {
            bytes_written = write(fd_dest, buffer, bytes_read);
            if (bytes_written != bytes_read) {
                perror("Error de escritura al copiar");
                close(fd_src); close(fd_dest);
                return 1;
            }
        }
        if (bytes_read == -1) {
            perror("Error de lectura al copiar");
        } else {
            printf("Archivo '%s' copiado a '%s' exitosamente.\n", target, dest);
        }
        close(fd_src);
        close(fd_dest);
    }
    // ==========================================
    // 9. CAMBIAR PERMISOS (System call: chmod)
    // ==========================================
    else if (strcmp(command, "chmod") == 0) {
        if (argc < 4) {
            printf("Error: Falta el modo octal (ej: 0755 o 0644).\n");
            return 1;
        }
        // Convertimos el string proporcionado por el usuario a un número entero base 8 (octal).
        mode_t mode = strtol(argv[3], NULL, 8);
        
        // chmod() aplica los nuevos permisos.
        if (chmod(target, mode) == -1) {
            perror("Error al cambiar permisos con chmod");
            return 1;
        }
        printf("Permisos de '%s' cambiados a %04o exitosamente.\n", target, mode);
    }
    // ==========================================
    // 10. ACTUALIZAR CONTENIDO (System call: write con O_TRUNC)
    // ==========================================
    else if (strcmp(command, "update") == 0) {
        if (argc < 4) {
            printf("Error: Falta el texto para actualizar el archivo.\n");
            return 1;
        }
        const char *text = argv[3];
        
        // O_TRUNC borra el contenido actual antes de escribir
        int fd = open(target, O_WRONLY | O_TRUNC);
        if (fd == -1) {
            perror("Error al abrir el archivo para actualizar");
            return 1;
        }
        
        ssize_t bytes_written = write(fd, text, strlen(text));
        if (bytes_written == -1) {
            perror("Error al actualizar el archivo");
            close(fd);
            return 1;
        }
        printf("Se actualizó el archivo '%s' escribiendo %zd bytes.\n", target, bytes_written);
        close(fd);
    }
    // Si el comando no coincide con nada de arriba
    else {
        printf("Comando '%s' no reconocido.\n", command);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
