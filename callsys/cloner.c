#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/*
 * =====================================================================================
 * Creador de Clones usando Hard Links (link)
 * =====================================================================================
 * Este programa toma un archivo y crea una "copia" con la extensión .clone.
 * 
 * En lugar de copiar los bytes uno a uno, utiliza la System Call link() para 
 * crear un "Hard Link" (enlace duro). Un hard link hace que el nuevo archivo 
 * apunte exactamente al mismo Inodo (bloque de datos físico) que el original.
 * 
 * Efecto:
 * Dado que ambos archivos son, a nivel de sistema operativo, el mismo archivo, 
 * si modificas el original, el clon mostrará automáticamente los cambios 
 * (y si modificas el clon, el original también cambia).
 * =====================================================================================
 */

/**
 * Función que crea un clon (hard link) del archivo especificado.
 * @param original Nombre o ruta del archivo original.
 * @return 0 si fue exitoso, -1 en caso de error.
 */
int create_clone(const char *original) {
    // Buscar si el archivo tiene una extensión (último punto)
    const char *dot = strrchr(original, '.');
    size_t base_len;
    
    // Si encontramos un punto (y no es el primer caracter, como en archivos ocultos)
    if (dot != NULL && dot != original) {
        base_len = dot - original; // Calculamos la longitud sin la extensión
    } else {
        base_len = strlen(original); // Si no hay extensión, tomamos todo el nombre
    }

    // Reservar memoria para el nuevo nombre: longitud base + ".clone" (6) + '\0' (1)
    char *clone_name = malloc(base_len + 7); 
    if (clone_name == NULL) {
        perror("Error al reservar memoria");
        return -1;
    }
    
    // Construir el nombre del clon reemplazando la extensión original por ".clone"
    strncpy(clone_name, original, base_len);
    clone_name[base_len] = '\0'; // Asegurar el terminador nulo
    strcat(clone_name, ".clone");

    // Invocar la llamada al sistema link()
    // -------------------------------------------------------------------------
    // ¿Qué es un Inodo? 
    // Un Inodo (Index Node) es una estructura interna de los sistemas de 
    // archivos UNIX/Linux que guarda toda la información y la ubicación en el 
    // disco de los datos reales, excepto el nombre del archivo. El nombre 
    // (ej: "archivo.txt") es simplemente un "puntero" o "etiqueta" hacia ese inodo.
    //
    // Al usar link(), creamos un 'Hard Link'. Esto significa que el nuevo 
    // nombre ('clone_name') apuntará EXACTAMENTE al mismo número de Inodo que 
    // tiene el archivo 'original'. Por lo tanto, no se duplican los datos,
    // simplemente ahora hay dos nombres apuntando al mismo bloque físico.
    // -------------------------------------------------------------------------
    if (link(original, clone_name) == -1) {
        perror("Error al crear el clon (hard link)");
        free(clone_name);
        return -1;
    }

    printf("¡Éxito! Se ha creado el clon: '%s'\n", clone_name);
    printf("-> Está vinculado a: '%s'\n", original);
    printf("-> Ambos archivos comparten el mismo Inodo.\n");
    printf("-> ¡Cualquier cambio en uno se reflejará en el otro al instante!\n");

    free(clone_name);
    return 0;
}

int main(int argc, char *argv[]) {
    // Validar cantidad de argumentos
    if (argc != 2) {
        printf("Uso: %s <archivo_original>\n", argv[0]);
        return 1;
    }

    const char *original_file = argv[1];
    
    // Llamar a la función, pasando el nombre del archivo como parámetro
    if (create_clone(original_file) == -1) {
        return 1; // Terminar con código de error si la función falló
    }

    return 0;
}
