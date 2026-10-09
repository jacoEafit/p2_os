#include "shell.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

// vuelca el archivo completo apuntado por 'fd' a un buffer dinámico (malloc)
static char *read_all_fd(int fd, off_t *out_size) {
    LOG_SYSCALL("lseek", "%d, 0, SEEK_END", fd);
    off_t size = lseek(fd, 0, SEEK_END); //offset a la posición final del archivo, guarda en size cuántos bytes ocupa
    if (size == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        perror("lseek");
        return NULL;
    }
    LOG_SYSCALL_RESULT(size);

    LOG_SYSCALL("lseek", "%d, 0, SEEK_SET", fd);
    off_t rc = lseek(fd, 0, SEEK_SET); //Devuelve offset a posición inicial para comenzar a leer bytes
    if (rc == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        perror("lseek");
        return NULL;
    }
    LOG_SYSCALL_RESULT(rc);

    // archivo vacío: se reserva 1 byte para devolver un buffer válido (malloc(0) puede dar NULL)
    size_t tamanoReserva = (size_t)size;
    if (size == 0) {
        tamanoReserva = 1;
    }

    //Crea un buffer con longitud de bytes del archivo, para cargar en memoria
    char *buffer = malloc(tamanoReserva);
    if (buffer == NULL) {
        printf(COLOR_ERROR "malloc: sin memoria disponible.\n" COLOR_RESET);
        return NULL;
    }
    
    //Almacena en memoria todo el archivo
    off_t total = 0;
    while (total < size) {
        LOG_SYSCALL("read", "%d, buffer+%ld, %ld", fd, (long)total, (long)(size - total));
        ssize_t leidos = read(fd, buffer + total, (size_t)(size - total));
        if (leidos < 0) {
            LOG_SYSCALL_ERROR(strerror(errno));
            perror("read");
            free(buffer);
            return NULL;
        }
        LOG_SYSCALL_RESULT(leidos);
        if (leidos == 0) break; // EOF inesperado
        total += leidos;
    }

    
    *out_size = total; //Guarda en variable out_size el total de bytes guardados en memoria
    return buffer; //Devuelve puntero a comienzo de contenido archivo guardado en memoria
}


// escribe exactamente los n bytes y reintenta ante writes parciales
static int write_all(int fd, const char *datos, size_t cantidad)
{
    size_t bytesEscritos = 0;

    while (bytesEscritos < cantidad) {
        ssize_t resultado = write(fd, datos + bytesEscritos, cantidad - bytesEscritos);
        if (resultado == -1) {
            perror("write");
            return -1;
        }
        bytesEscritos += (size_t)resultado;  // se pasa a size_t porque es != 1  
    }
    return 0;
}

// abrir archivo -> o nombreArchivo
int cmd_o(int argc, char *argv[], Editor *editor)
{
    if (argc != 2) {
        printf(COLOR_ERROR "Uso: o [nombre_archivo.ext]\n" COLOR_RESET);

        return -1;
    }

    const char *nArchivo = argv[1];

    // cerrar el archivo anterior en caso de que haya uno ya abierto
    if (editor->fd != -1) {
        LOG_SYSCALL("close", "%d", editor->fd);
        int rc = close(editor->fd);
        if (rc == -1) {
            LOG_SYSCALL_ERROR(strerror(errno));
            perror("close");
        } else {
            LOG_SYSCALL_RESULT(rc);
        }
        editor->fd = -1;  // file descriptor = -1, indica que no hay archivo abierto
    }

    // abrir el nuevo archivo y guardar su estado en el struct editor
    LOG_SYSCALL("open", "%s, O_RDWR | O_CREAT, 0644", nArchivo);
    editor->fd = open(nArchivo, O_RDWR | O_CREAT, 0644);
    if (editor->fd == -1) {
        perror("open");  // manejo de errores en caso de no poderlo abrir

        return -1;
    }
    LOG_SYSCALL_RESULT(editor->fd);  // imprime lo que devuelve el syscall open

    printf(COLOR_RESULT "Archivo abierto: %s\n" COLOR_RESET, nArchivo);

    return 0;
}

// imprime la linea n -> p [n] | o todo -> solo p
int cmd_p(int argc, char *argv[], Editor *editor)
{
    if (editor->fd == -1) {  // verificar que haya archivo abierto
        printf(COLOR_ERROR "No hay un archivo abierto.\n" COLOR_RESET);

        return -1;
    }

    if (argc > 2) { // verificar que el numero de argumentos este correcto
        printf(COLOR_ERROR "Uso: p [n]\n" COLOR_RESET);

        return -1;
    }

    // validar n 
    int lineaObjetivo = 0;
    if (argc == 2) {
        lineaObjetivo = atoi(argv[1]);
        if (lineaObjetivo <= 0) {
            printf(COLOR_ERROR "Número de línea inválido.\n" COLOR_RESET);

            return -1;
        }
    }

    // cargar todo el archivo en memoria usando la funcion auxiliar (lseek + lseek + read)
    off_t tamano = 0;
    char *contenido = read_all_fd(editor->fd, &tamano);
    if (contenido == NULL) {
        return -1;
    }

    // p sin argumento: imprimir todo
    if (argc == 1) {
        int resultado = write_all(STDOUT_FILENO, contenido, (size_t)tamano);
        free(contenido);

        return resultado;
    }

    // p n: buscar el byte donde empieza la línea n
    off_t posicion = 0;
    int lineaActual = 1;
    while (posicion < tamano && lineaActual < lineaObjetivo) {

        if (contenido[posicion] == '\n') {
            lineaActual++;
        }

        posicion++;
    }

    if (lineaActual < lineaObjetivo || posicion >= tamano) {
        printf(COLOR_ERROR "La línea %d no existe en el archivo.\n" COLOR_RESET, lineaObjetivo);
        free(contenido);

        return -1;
    }

    // buscar el final de la línea elegida
    off_t finLinea = posicion;

    while (finLinea < tamano && contenido[finLinea] != '\n') {
        finLinea++;
    }

    int resultado = write_all(STDOUT_FILENO, contenido + posicion, (size_t)(finLinea - posicion));
    free(contenido);

    return resultado;

}

// añade "texto" como nueva linea al final del archivo -> a [texto]
int cmd_a(int argc, char *argv[], Editor *editor)
{
    if (editor->fd == -1) { // verificar que haya archivo abierto
        printf(COLOR_ERROR "No hay un archivo abierto. Usa 'o <archivo>' primero.\n" COLOR_RESET);
        return -1;
    }

    if (compresor_fd_en_uso(editor->fd)) {
        printf(COLOR_ERROR "El archivo está siendo procesado por el compresor; no se puede modificar. Usa es o stop.\n" COLOR_RESET);
        return -1;
    }

    if (argc < 2) { // verificar que llegue al menos una palabra de texto
        printf(COLOR_ERROR "Uso: a [texto]\n" COLOR_RESET);
        return -1;
    }

    // Contar bytes de texto ingresado
    size_t textoLen = 0;
    for (int i = 1; i < argc; i++) {
        textoLen += strlen(argv[i]) + 1; // +1 por el espacio o el nulo final
    }

    //Reservamos espacio de memoria variable para almacenar ese texto, mediante malloc
    char *texto = malloc(textoLen);
    if (texto == NULL) {
        printf(COLOR_ERROR "malloc: sin memoria disponible.\n" COLOR_RESET);
        return -1;
    }

    //Itera los argumentos del texto que ingresó el usuario, los guarda en memoria previamente reservada
    texto[0] = '\0';
    for (int i = 1; i < argc; i++) {
        strcat(texto, argv[i]);
        if (i != argc - 1) {
            strcat(texto, " ");
        }
    }
    textoLen = strlen(texto);

    // Calcular el tamaño del archivo
    LOG_SYSCALL("lseek", "%d, 0, SEEK_END", editor->fd);
    off_t tamano = lseek(editor->fd, 0, SEEK_END);
    if (tamano == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        perror("lseek");
        free(texto);
        return -1;
    }
    LOG_SYSCALL_RESULT(tamano);

    // si el archivo no termina en '\n' hay que agregarlo antes, si no el texto quedaria pegado a la ultima linea
    int faltaSalto = 0;
    if (tamano > 0) {
        LOG_SYSCALL("lseek", "%d, -1, SEEK_END", editor->fd);
        off_t rc = lseek(editor->fd, -1, SEEK_END);
        if (rc == -1) {
            LOG_SYSCALL_ERROR(strerror(errno));
            perror("lseek");
            free(texto);
            return -1;
        }
        LOG_SYSCALL_RESULT(rc);

        char ultimo;
        LOG_SYSCALL("read", "%d, &ultimo, 1", editor->fd);
        ssize_t leidos = read(editor->fd, &ultimo, 1); // despues de leer, el offset queda otra vez al final
        if (leidos == -1) {
            LOG_SYSCALL_ERROR(strerror(errno));
            perror("read");
            free(texto);
            return -1;
        }
        LOG_SYSCALL_RESULT(leidos);

        if (ultimo != '\n') {
            faltaSalto = 1;
        }
    }

    // nuevo buffer: ['\n' opcional] + texto + '\n'
    size_t nuevoTamano = (size_t)faltaSalto + textoLen + 1;
    char *nuevaLinea = malloc(nuevoTamano);
    if (nuevaLinea == NULL) {
        printf(COLOR_ERROR "malloc: sin memoria disponible.\n" COLOR_RESET);
        free(texto);
        return -1;
    }

    if (faltaSalto) {
        nuevaLinea[0] = '\n';                                   // cerrar la ultima linea existente
    }
    memcpy(nuevaLinea + faltaSalto, texto, textoLen);           // texto nuevo
    nuevaLinea[nuevoTamano - 1] = '\n';                         // fin de la linea nueva

    // escribir al final (write_all repite el write si queda parcial)
    LOG_SYSCALL("write", "%d, nuevaLinea, %zu", editor->fd, nuevoTamano);
    if (write_all(editor->fd, nuevaLinea, nuevoTamano) == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        free(texto);
        free(nuevaLinea);
        return -1;
    }
    LOG_SYSCALL_RESULT(nuevoTamano); // si write_all no fallo, se escribieron todos los bytes

    printf(COLOR_RESULT "Línea añadida al final (%zu bytes añadidos).\n" COLOR_RESET, nuevoTamano);

    free(texto);
    free(nuevaLinea);
    return 0;
}

// borra la linea n desplazando los bytes posteriores y truncando -> d [n]
int cmd_d(int argc, char *argv[], Editor *editor)
{
    if (editor->fd == -1) { // verificar que haya archivo abierto
        printf(COLOR_ERROR "No hay un archivo abierto. Usa 'o <archivo>' primero.\n" COLOR_RESET);
        return -1;
    }

    if (compresor_fd_en_uso(editor->fd)) {
        printf(COLOR_ERROR "El archivo está siendo procesado por el compresor; no se puede modificar. Usa es o stop.\n" COLOR_RESET);
        return -1;
    }

    if (argc != 2) { // verificar que llegue exactamente n
        printf(COLOR_ERROR "Uso: d [n]\n" COLOR_RESET);
        return -1;
    }

    // validar si es valor negativo
    int lineaObjetivo = atoi(argv[1]);
    if (lineaObjetivo <= 0) {
        printf(COLOR_ERROR "Número de línea inválido.\n" COLOR_RESET);
        return -1;
    }

    // cargar todo el archivo en memoria usando la funcion auxiliar
    off_t tamano = 0;
    char *contenido = read_all_fd(editor->fd, &tamano);
    if (contenido == NULL) {
        return -1;
    }

    // buscar el byte donde empieza la linea n
    off_t posicion = 0;
    int lineaActual = 1;
    while (posicion < tamano && lineaActual < lineaObjetivo) {
        if (contenido[posicion] == '\n') {
            lineaActual++;
        }
        posicion++;
    }

    if (lineaActual < lineaObjetivo || posicion >= tamano) {
        printf(COLOR_ERROR "La línea %d no existe en el archivo.\n" COLOR_RESET, lineaObjetivo);
        free(contenido);
        return -1;
    }

    // buscar el final de la linea, incluyendo su '\n' para no dejar una linea vacia
    off_t finLinea = posicion;
    while (finLinea < tamano && contenido[finLinea] != '\n') {
        finLinea++;
    }
    if (finLinea < tamano) {
        finLinea++;
    }

    off_t bytesBorrados = finLinea - posicion;
    off_t bytesPosteriores = tamano - finLinea;
    off_t nuevoTamano = tamano - bytesBorrados;

    // ubicarse donde empieza la linea a borrar
    LOG_SYSCALL("lseek", "%d, %ld, SEEK_SET", editor->fd, (long)posicion);
    off_t rc = lseek(editor->fd, posicion, SEEK_SET);
    if (rc == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        perror("lseek");
        free(contenido);
        return -1;
    }
    LOG_SYSCALL_RESULT(rc);

    // desplazar hacia atras los bytes que venian despues de la linea (si se borra la ultima no hay nada que mover)
    if (bytesPosteriores > 0) {
        LOG_SYSCALL("write", "%d, contenido+%ld, %ld", editor->fd, (long)finLinea, (long)bytesPosteriores);
        if (write_all(editor->fd, contenido + finLinea, (size_t)bytesPosteriores) == -1) {
            LOG_SYSCALL_ERROR(strerror(errno));
            free(contenido);
            return -1;
        }
        LOG_SYSCALL_RESULT(bytesPosteriores); // si write_all no fallo, se escribieron todos los bytes
    }

    // cortar los bytes que sobran al final del archivo
    LOG_SYSCALL("ftruncate", "%d, %ld", editor->fd, (long)nuevoTamano);
    int trc = ftruncate(editor->fd, nuevoTamano);
    if (trc == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        perror("ftruncate");
        free(contenido);
        return -1;
    }
    LOG_SYSCALL_RESULT(trc);

    printf(COLOR_RESULT "Línea %d borrada (%ld bytes eliminados).\n" COLOR_RESET,
           lineaObjetivo, (long)bytesBorrados);

    free(contenido);
    return 0;
}

// cierra el file descriptor del archivo abierto y vuelve a la shell -> q
int cmd_q(int argc, char *argv[], Editor *editor)
{
    (void)argv;

    if (argc != 1) { // q no recibe argumentos
        printf(COLOR_ERROR "Uso: q\n" COLOR_RESET);
        return -1;
    }

    if (editor->fd == -1) { // verificar que haya archivo abierto
        printf(COLOR_ERROR "No hay un archivo abierto.\n" COLOR_RESET);
        return -1;
    }

    // el editor solo guarda el fd (no hay memoria dinamica pendiente), basta con cerrarlo
    LOG_SYSCALL("close", "%d", editor->fd);
    int rc = close(editor->fd);
    editor->fd = -1;  // aun si close falla, el fd ya no se debe usar
    if (rc == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        perror("close");
        return -1;
    }
    LOG_SYSCALL_RESULT(rc);

    printf(COLOR_RESULT "Archivo cerrado.\n" COLOR_RESET);
    return 0;
}

// inserta "texto" como nueva linea antes de la linea n -> i [n] [texto]
int cmd_i(int argc, char *argv[], Editor *editor)
{
    if (editor->fd == -1) { // verificar que haya archivo abierto
        printf(COLOR_ERROR "No hay un archivo abierto. Usa 'o <archivo>' primero.\n" COLOR_RESET);
        return -1;
    }

    if (compresor_fd_en_uso(editor->fd)) {
        printf(COLOR_ERROR "El archivo está siendo procesado por el compresor; no se puede modificar. Usa es o stop.\n" COLOR_RESET);
        return -1;
    }

    if (argc < 3) { // verificar que lleguen n y al menos una palabra de texto
        printf(COLOR_ERROR "Uso: i [n] [texto]\n" COLOR_RESET);
        return -1;
    }

    // validar n
    int lineaObjetivo = atoi(argv[1]);
    if (lineaObjetivo <= 0) {
        printf(COLOR_ERROR "Número de línea inválido.\n" COLOR_RESET);
        return -1;
    }

    // unir los argumentos restantes en un solo texto separado por espacios
    size_t textoLen = 0;
    for (int i = 2; i < argc; i++) {
        textoLen += strlen(argv[i]) + 1; // +1 por el espacio o el nulo final
    }

    char *texto = malloc(textoLen);
    if (texto == NULL) {
        printf(COLOR_ERROR "malloc: sin memoria disponible.\n" COLOR_RESET);
        return -1;
    }

    texto[0] = '\0';
    for (int i = 2; i < argc; i++) {
        strcat(texto, argv[i]);
        if (i != argc - 1) {
            strcat(texto, " ");
        }
    }
    textoLen = strlen(texto);

    // cargar todo el archivo en memoria usando la funcion auxiliar (lseek + lseek + read)
    off_t tamano = 0;
    char *contenido = read_all_fd(editor->fd, &tamano);
    if (contenido == NULL) {
        free(texto);
        return -1;
    }

    // buscar el byte donde empieza la linea n (ahi se inserta el texto)
    off_t posicion = 0;
    int lineaActual = 1;
    while (posicion < tamano && lineaActual < lineaObjetivo) {
        if (contenido[posicion] == '\n') {
            lineaActual++;
        }
        posicion++;
    }

    // si el archivo termino antes de llegar a la linea n, esa posicion no existe
    if (lineaActual < lineaObjetivo) {
        printf(COLOR_ERROR "La línea %d no existe en el archivo.\n" COLOR_RESET, lineaObjetivo);
        free(texto);
        free(contenido);
        return -1;
    }

    // nuevo buffer: [0, posicion) + texto + '\n' + [posicion, tamano)
    off_t nuevoTamano = tamano + (off_t)textoLen + 1;
    char *nuevoContenido = malloc((size_t)nuevoTamano);
    if (nuevoContenido == NULL) {
        printf(COLOR_ERROR "malloc: sin memoria disponible.\n" COLOR_RESET);
        free(texto);
        free(contenido);
        return -1;
    }

    memcpy(nuevoContenido, contenido, (size_t)posicion);                 // parte anterior
    memcpy(nuevoContenido + posicion, texto, textoLen);                  // texto nuevo
    nuevoContenido[posicion + (off_t)textoLen] = '\n';                   // fin de la linea nueva
    memcpy(nuevoContenido + posicion + (off_t)textoLen + 1,
           contenido + posicion, (size_t)(tamano - posicion));           // parte posterior

    // volver al inicio del archivo para reescribirlo completo
    LOG_SYSCALL("lseek", "%d, 0, SEEK_SET", editor->fd);
    off_t rc = lseek(editor->fd, 0, SEEK_SET);
    if (rc == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        perror("lseek");
        free(texto);
        free(contenido);
        free(nuevoContenido);
        return -1;
    }
    LOG_SYSCALL_RESULT(rc);

    // escribir el contenido nuevo completo (write_all repite el write si queda parcial)
    LOG_SYSCALL("write", "%d, nuevoContenido, %ld", editor->fd, (long)nuevoTamano);
    if (write_all(editor->fd, nuevoContenido, (size_t)nuevoTamano) == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        free(texto);
        free(contenido);
        free(nuevoContenido);
        return -1;
    }
    LOG_SYSCALL_RESULT(nuevoTamano); // si write_all no fallo, se escribieron todos los bytes

    printf(COLOR_RESULT "Línea insertada en la posición %d (%zu bytes añadidas).\n" COLOR_RESET,
           lineaObjetivo, textoLen + 1);

    free(texto);
    free(contenido);
    free(nuevoContenido);
    return 0;
}

// busca todas las apariciones de "palabra" e informa linea y columna -> s [palabra]
int cmd_s(int argc, char *argv[], Editor *editor)
{
    if (editor->fd == -1) { // verificar que haya archivo abierto
        printf(COLOR_ERROR "No hay un archivo abierto. Usa 'o <archivo>' primero.\n" COLOR_RESET);
        return -1;
    }

    if (argc != 2) { // verificar que llegue exactamente una palabra
        printf(COLOR_ERROR "Uso: s [palabra]\n" COLOR_RESET);
        return -1;
    }

    const char *palabra = argv[1];
    size_t palabraLen = strlen(palabra);
    if (palabraLen == 0) { // pasa si el usuario escribe s ""
        printf(COLOR_ERROR "La palabra a buscar no puede estar vacía.\n" COLOR_RESET);
        return -1;
    }

    // cargar todo el archivo en memoria usando la funcion auxiliar (lseek + lseek + read)
    off_t tamano = 0;
    char *contenido = read_all_fd(editor->fd, &tamano);
    if (contenido == NULL) {
        return -1;
    }

    printf(COLOR_TITLE "--- Resultados de búsqueda de \"%s\" ---\n" COLOR_RESET, palabra);

    int lineaActual = 1;
    long columnaActual = 1;
    int coincidencias = 0;

    // recorrer el buffer byte a byte
    for (off_t posicion = 0; posicion < tamano; posicion++) {
        unsigned char byte = (unsigned char)contenido[posicion];

        if (byte == '\n') { // nueva linea: reinicia la columna
            lineaActual++;
            columnaActual = 1;
            continue;
        }

        // comparar solo si caben todos los bytes de la palabra desde esta posicion
        if ((size_t)(tamano - posicion) >= palabraLen &&
            memcmp(contenido + posicion, palabra, palabraLen) == 0) {
            printf("  " COLOR_RESULT "Coincidencia" COLOR_RESET " en línea " COLOR_PARAM "%d" COLOR_RESET
                   ", columna " COLOR_PARAM "%ld" COLOR_RESET "\n", lineaActual, columnaActual);
            coincidencias++;
        }

        // parte necesaria para abordar letras de UTF-8
        if ((byte & 0xC0) != 0x80) {
            columnaActual++;
        }
    }

    if (coincidencias == 0) {
        printf(COLOR_INFO "No se encontraron coincidencias.\n" COLOR_RESET);
    } else {
        printf(COLOR_TITLE "Total de coincidencias: " COLOR_RESULT "%d\n" COLOR_RESET, coincidencias);
    }

    free(contenido);
    return 0;
}
