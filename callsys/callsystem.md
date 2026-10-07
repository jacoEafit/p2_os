# Uso de Llamadas al Sistema (System Calls) en C

Las **llamadas al sistema (System Calls o Syscalls)** son la interfaz fundamental entre una aplicación de usuario y el Kernel (núcleo) del sistema operativo. Mientras que las funciones estándar de C (como `printf` o `fopen`) viven en el espacio de usuario, las llamadas al sistema (como `write` o `open`) le piden directamente al Kernel que realice una tarea privilegiada, como acceder al hardware, manejar memoria o, en nuestro caso, **gestionar el sistema de archivos**.

En este documento explicaremos cómo utilizar las llamadas al sistema POSIX en C, usando como referencia los ejemplos de nuestro programa `file_manager.c`.

### Comparativa: System Calls vs Librería Estándar de C
A continuación, una tabla que compara las llamadas al sistema (Syscalls) que hemos usado, con sus contrapartes en la librería estándar de C (`stdio.h`), las cuales internamente (en sistemas UNIX/Linux) terminan invocando a estas Syscalls.

| Operación | System Call (Kernel / POSIX) | Función de Librería Estándar de C | Comando de Terminal (CLI) | Diferencia Principal |
| :--- | :--- | :--- | :--- | :--- |
| **Abrir / Crear** | `open()` | `fopen()` | `touch`, `cat >` | `open` devuelve un entero (`fd`), `fopen` devuelve un puntero (`FILE *`) que incluye un buffer gestionado en memoria de usuario. |
| **Cerrar** | `close()` | `fclose()` | *(Implicito al terminar comando)* | `fclose` limpia y vacía el buffer de usuario (flush) antes de invocar internamente a `close`. |
| **Leer** | `read()` | `fread()`, `fgets()`, `fscanf()` | `cat`, `less`, `more` | `read` extrae bytes crudos directo del kernel; las de librería permiten parsear texto o tipos de datos. |
| **Escribir (Añadir)** | `write()` con `O_APPEND` | `fwrite()`, `fputs()`, `fprintf()` | `echo "..." >>`, `tee -a` | `write` envía bloques de bytes crudos; `fprintf` permite formatear cadenas dinámicamente. |
| **Actualizar (Sobrescribir)** | `open()` con `O_TRUNC` y `write()` | `freopen()`, `fprintf()` | `echo "..." >` | Al usar `O_TRUNC` al abrir, se vacía el archivo antes de escribir. |
| **Borrar Archivo** | `unlink()` | `remove()` | `rm` | `remove()` es el estándar de C, el cual en sistemas POSIX hace una llamada directa a `unlink()`. Comandos como `rm` utilizan esta syscall. |
| **Metadatos / Info** | `stat()` | *No hay equivalente estándar* | `stat`, `ls -l` | El estándar de C básico no lee inodos; se requiere usar la API POSIX. |
| **Directorios** | `mkdir()`, `rmdir()` | *No hay equivalente estándar* | `mkdir`, `rmdir` | La librería estándar de C pura no gestiona carpetas, se delega al estándar POSIX del OS. |

---

## 1. El concepto de "File Descriptor"

Cuando utilizas una llamada al sistema para abrir un archivo, el Kernel no te devuelve un objeto complejo (como el `FILE *` de C estándar). En su lugar, te devuelve un **File Descriptor (fd)**, que no es más que un número entero (int). 

El Kernel usa este número como un índice en una tabla interna para saber a qué archivo físico te refieres cuando haces operaciones posteriores (como leer o escribir).

* **0** = Entrada estándar (stdin)
* **1** = Salida estándar (stdout)
* **2** = Salida de error (stderr)
* **>2** = Archivos que tu programa abre (como `3`, `4`, etc.)

---

## 2. Abriendo y Creando Archivos (`open` y `close`)

La función `open()` se utiliza tanto para abrir archivos existentes como para crear nuevos. 

```c
#include <fcntl.h>

// Ejemplo en file_manager.c:
int fd = open(target, O_CREAT | O_WRONLY | O_TRUNC, 0644);
```

### Explicación de los argumentos:
1. **`target`**: La ruta del archivo.
2. **`Flags (Banderas)`**: 
   * `O_CREAT`: Le dice al Kernel que si el archivo no existe, lo cree.
   * `O_WRONLY`: Abre el archivo solo para escritura (Write Only).
   * `O_TRUNC`: Si el archivo ya existe, borra su contenido anterior para empezar de cero.
   * *Existen otros como `O_RDONLY` (solo lectura) o `O_APPEND` (escribir al final).*
3. **`0644` (Modo)**: Son los permisos octales con los que se creará el archivo. `6` (lectura/escritura para el dueño) y `4` (solo lectura para el grupo y otros).

Siempre que termines de usar un archivo, **debes cerrarlo** para que el Kernel libere ese File Descriptor:
```c
close(fd);
```

---

## 3. Escribiendo y Leyendo (`write` y `read`)

A diferencia de `fprintf` o `fscanf`, las llamadas `write` y `read` trabajan directamente con bloques de bytes sin formato.

### Escribir datos:
```c
#include <unistd.h>

// Ejemplo en file_manager.c:
ssize_t bytes_written = write(fd, text, strlen(text));
```
* **`fd`**: El File Descriptor devuelto por `open()`.
* **`text`**: El puntero a los datos que quieres escribir.
* **`strlen(text)`**: La cantidad exacta de bytes que el Kernel debe tomar de la memoria de tu programa e insertar en el archivo.

### Leer datos:
La lectura normalmente se hace usando un "buffer" (un array de memoria temporal) dentro de un ciclo `while`.
```c
char buffer[1024]; // Memoria temporal
ssize_t bytes_read;

// read() devuelve cuántos bytes leyó realmente. Retorna 0 si llegó al final del archivo.
while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytes_read] = '\0'; // Lo convertimos en string de C
    printf("%s", buffer);      // Lo imprimimos en pantalla
}
```

---

## 4. Obtener metadatos (`stat`)

Si quieres saber el tamaño, el tipo o los permisos de un archivo, usas `stat()`. El Kernel llenará una estructura de tipo `struct stat` con la información del inodo (el bloque de información interna del archivo en el disco).

```c
#include <sys/stat.h>

struct stat st;
if (stat(target, &st) == -1) {
    perror("Error");
} else {
    printf("Tamaño: %ld bytes\n", st.st_size);
    printf("Inodo: %ld\n", st.st_ino);
    
    // Podemos usar macros provistas por POSIX para saber qué es:
    if (S_ISDIR(st.st_mode)) {
        printf("Es un Directorio\n");
    } else if (S_ISREG(st.st_mode)) {
        printf("Es un Archivo regular\n");
    }
}
```

---

## 5. Gestión del sistema de archivos (`mkdir`, `rmdir`, `unlink`, `chmod`)

Estas llamadas al sistema interactúan con la jerarquía de carpetas y enlaces sin necesidad de "abrir" el archivo:

* **Crear un directorio**: `mkdir(ruta, permisos_octales)`
  ```c
  mkdir("mi_carpeta", 0755);
  ```

* **Eliminar un directorio (debe estar vacío)**: `rmdir(ruta)`
  ```c
  rmdir("mi_carpeta");
  ```

* **Eliminar un archivo (`rm`)**: En UNIX, eliminar un archivo es realmente "desenlazarlo" (quitarle su nombre). Si ningún proceso lo está usando, el Kernel borra los datos. Es la system call utilizada por el comando `rm`.
  ```c
  unlink("archivo.txt");
  ```

* **Cambiar permisos**: Modifica directamente los permisos de acceso del Inodo.
  ```c
  // mode es un número entero derivado de un octal (ej: 0777)
  chmod("archivo.txt", mode); 
  ```

---

### Resumen
Usar las System Calls nos permite entender exactamente cómo funciona el sistema operativo por debajo. Cada vez que usas utilidades comunes como `cp`, `ls` o `mkdir` en la terminal, esos programas están usando por detrás exactamente estas mismas funciones en C que acabamos de implementar en `file_manager.c`.
