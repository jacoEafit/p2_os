# Shell Educativo para la Exploración de Llamadas al Sistema (Linux)

Este proyecto es una herramienta diseñada en **C** para estudiantes de Sistemas Operativos. Consiste en una interfaz de línea de comandos (CLI) interactiva que clasifica y explica de forma visual las **llamadas al sistema (system calls)** básicas que realiza el kernel de Linux.

Cada comando educativo del shell simula o realiza operaciones comunes de un sistema operativo, y muestra en tiempo real (estilo `strace` simplificado) la llamada al sistema que ejecuta detrás de escena, incluyendo los parámetros que recibe y el valor devuelto.

---

## Estructura de Comandos por Categoría

### 1. Categoría: Datos y Archivos (`datos`)

Permite entender cómo interactúa el espacio de usuario con el sistema de archivos del kernel.

* `d_create <archivo> "<texto>"`: Abre/crea un archivo y escribe un texto en él.
  * *Syscalls*: `open(2)`, `write(2)`, `close(2)`
* `d_read <archivo>`: Abre un archivo para lectura, lee su contenido y lo imprime en pantalla.
  * *Syscalls*: `open(2)`, `read(2)`, `close(2)`
* `d_info <archivo>`: Lee y detalla metadatos de un archivo como inodo, permisos, dueño y tamaño.
  * *Syscalls*: `stat(2)`
* `d_copy <origen> <destino>`: Realiza una copia bloque por bloque de un archivo.
  * *Syscalls*: `open(2)`, `read(2)`, `write(2)`, `close(2)`

### 2. Categoría: Memoria (`memoria`)

Explora el comportamiento de asignación dinámica de memoria del proceso del shell.

* `m_sbrk <incremento>`: Ajusta el program break del proceso (heap). Útil para ver cómo crece y decrece el espacio de datos en bruto.
  * *Syscalls*: `sbrk(2)`
* `m_mmap <tamaño_bytes>`: Crea un mapeo de memoria anónima y privada en el espacio de direcciones, escribe un patrón de caracteres y luego la libera.
  * *Syscalls*: `mmap(2)`, `munmap(2)`
* `m_info`: Muestra estadísticas detalladas del consumo y estado de memoria del propio shell.
  * *Mecanismo*: Lectura directa del archivo `/proc/self/status` del kernel.

### 3. Categoría: Monitoreo y Procesos (`monitoreo`)

Visualiza la jerarquía de procesos, ejecución de programas y señales de Linux.

* `p_fork`: Clona el proceso shell. El proceso hijo duerme 2 segundos y retorna con código `42`, mientras que el padre espera su término.
  * *Syscalls*: `fork(2)`, `getpid(2)`, `getppid(2)`, `waitpid(2)`
* `p_exec <comando> [args...]`: Es el núcleo de cualquier shell tradicional. Crea un hijo y este reemplaza su imagen de memoria con un ejecutable de Linux.
  * *Syscalls*: `fork(2)`, `execvp(3)`, `waitpid(2)`
* `p_kill <pid> <numero_señal>`: Envía una señal IPC a un proceso.
  * *Syscalls*: `kill(2)`
* `p_monitor`: Muestra información estadística del uso de recursos del propio proceso (tiempos de CPU, fallos de página, etc.).
  * *Syscalls*: `getrusage(2)`

### 4. Categoría: Utilidades (`utilidades`)

Comandos útiles para ver la hora, la fecha y saludar al usuario actual.

* `saludar`: Saluda al usuario actual leyendo su información de UID.
  * *Syscalls*: `getuid(2)`
* `hora`: Muestra la hora del sistema en formato HH:MM:SS.
  * *Syscalls*: `time(2)`
* `fecha`: Muestra la fecha del sistema en formato YYYY-MM-DD.
  * *Syscalls*: `time(2)`

---

## Compilación y Ejecución

Para compilar el proyecto es necesario estar en un entorno Linux con GCC y Make instalados.

1. **Compilar el proyecto:**

   ```bash
   make
   ```

   Esto producirá el binario ejecutable `sys_shell`.
2. **Iniciar el shell interactivo:**

   ```bash
   ./sys_shell
   ```
3. **Limpiar archivos objeto y binarios:**

   ```bash
   make clean
   ```

---

## Ejemplo de Uso Educativo

Cuando ejecutas un comando en `sys_shell`, verás las syscalls ejecutadas en color **magenta** en la terminal:

```ansi
sys-shell> d_create mi_archivo.txt "Hola SO"
[syscall] open("mi_archivo.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) ... = 3
[syscall] write(3, "Hola SO", 7) ... = 7
[syscall] close(3) ... = 0
Archivo 'mi_archivo.txt' creado exitosamente con 7 bytes.

sys-shell> d_info mi_archivo.txt
[syscall] stat("mi_archivo.txt", &st) ... = 0
--- Metadatos del Archivo (stat) ---
  Inodo:          456789
  Permisos (oct): 644
  Tamaño:         7 bytes
  MTime (Modif):  Mon Jul 27 13:50:00 2026
------------------------------------
```

---

## Ayuda Interactiva del Shell

El shell cuenta con una sección de ayuda dinámica muy completa. Puedes escribir:

* `help`: Muestra la bienvenida y el listado de categorías.
* `help <categoria>`: Muestra todos los comandos pertenecientes a `datos`, `memoria`, `monitoreo` o `utilidades`.
* `help <comando>`: Explica individualmente qué hace un comando, cómo se usa y qué llamadas al sistema específicas invoca.
