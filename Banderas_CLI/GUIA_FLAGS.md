# Guía Académica: Paso de Argumentos y Banderas en C

Esta guía explica en detalle cómo gestionan los sistemas operativos tipo Unix (como Linux) la transferencia de argumentos desde la línea de comandos hacia los programas escritos en C.

---

## 1. ¿Cómo llegan los argumentos a un programa C? (`argc` y `argv`)

El punto de entrada estándar de un programa en C es la función `main`:

```c
int main(int argc, char *argv[])
```

* **`argc` (Argument Count)**: Un entero mayor o igual a 1 que indica cuántos elementos se pasaron al programa. Siempre incluye el nombre del propio ejecutable.
* **`argv` (Argument Vector)**: Un arreglo de punteros a cadenas de caracteres (strings). Cada puntero apunta a un argumento delimitado por espacios en la terminal. Por convención:
  * `argv[0]` es la ruta o nombre del ejecutable.
  * `argv[argc]` es garantizado que sea `NULL`.

---

## 2. Mapa del Stack de Memoria al iniciar el proceso

Cuando ejecutas un comando en la shell (ej. `ls -la /tmp`), el kernel clona el proceso (`fork`) y ejecuta una llamada `execve(2)` para reemplazar la imagen con el binario de `ls`.

El kernel de Linux construye en la parte superior del **Stack de Memoria (Pila)** del nuevo proceso el siguiente mapa físico antes de transferir el control a `main`:

```text
  Direcciones Altas (Fin de la memoria virtual)
  +-------------------------------------------------------+
  | Variables de Entorno (envp[0], envp[1]...)            |
  +-------------------------------------------------------+
  | Cadenas de Argumentos (argv[0], argv[1]...)           | E.g., "./cli_parser\0", "manual\0"
  +-------------------------------------------------------+
  | envp[] (Arreglo de punteros a variables de entorno)   | Terminado en NULL
  +-------------------------------------------------------+
  | argv[] (Arreglo de punteros a las cadenas anteriores) | Terminado en NULL
  +-------------------------------------------------------+
  | argc (Entero)                                         | <-- El puntero de pila (ESP/RSP) apunta aquí
  +-------------------------------------------------------+
  Direcciones Bajas
```

Es por esto que las direcciones de memoria impresas por `cli_parser` en el trazado `[Trazado]` están muy juntas y localizadas en el rango del stack.

---

## 3. Comparativa de Métodos de Parseo

### A. Parseo Manual

* **Ventajas**: No tiene dependencias; es fácil de implementar para una sola bandera simple.
* **Desventajas**: Se vuelve sumamente complejo y propenso a fallos (ej. accesos a memoria fuera de rango si falta un argumento al final) al validar múltiples banderas combinadas o formatos cruzados (como `-vp 8080`).

### B. POSIX `getopt(3)`

* **Funcionamiento**: Realiza un ordenamiento de los argumentos no-opciones al final y procesa las banderas identificadas por un único carácter antecedido por `-`.
* **Variables clave**:
  * `optarg`: Si el carácter de opción espera un parámetro (definido con `:` en la cadena de formato, ej: `n:`), `optarg` almacena el puntero al valor (ej. `argv[optind-1]`).
  * `optind`: Almacena el índice en `argv` del siguiente elemento a procesar. Al finalizar el ciclo `while`, cualquier argumento que no sea una bandera (argumentos posicionales) quedará desplazado a partir de `argv[optind]`.
  * `optopt`: Guarda el carácter en caso de una opción inválida o faltante.
  * `opterr`: Si se define en `0`, silencia los mensajes de error por defecto de `getopt`.

### C. GNU `getopt_long(3)`

* **Funcionamiento**: Permite banderas largas descriptivas antecedidas por `--` y combinarlas con las opciones cortas tradicionales.
* **La estructura `struct option`**:
  ```c
  struct option {
      const char *name;    /* Nombre largo sin los guiones (ej. "port") */
      int         has_arg; /* required_argument (1), no_argument (0), optional_argument (2) */
      int        *flag;    /* NULL para retornar el caracter corta 'val'; o dirección de variable */
      int         val;     /* Caracter corto equivalente (ej. 'p') */
  };
  ```

---

## 4. Ejercicios Didácticos Propuestos

1. **Argumentos Posicionales**:
   Ejecuta el modo `getopt` de la siguiente forma:
   ```bash
   ./cli_parser getopt -v -n Edison archivo1.txt archivo2.txt
   ```
   Identifica qué imprime el programa en la sección *Argumentos posicionales adicionales*. Explica qué valor tomó la variable global `optind` del sistema.
2. **Combinación de banderas cortas**:
   Ejecuta `./cli_parser getopt -vn Edison` (observa cómo se agruparon las opciones `-v` y `-n`). ¿Funciona correctamente en el modo `getopt`? ¿Funcionaría en el modo `manual`? Explica por qué.
3. **Banderas con signo de asignación (`=`)**:
   En el modo `getopt_long`, ejecuta el comando con la sintaxis:
   ```bash
   ./cli_parser getopt_long --name=Edison --port=80
   ```
   Verifica si `getopt_long` es capaz de extraer el valor a pesar de estar unido con el signo `=`.
