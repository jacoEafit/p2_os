# Banderas_CLI: Parseo de Argumentos en Línea de Comandos en C

Este subproyecto enseña de manera práctica y visual cómo leer, validar y procesar parámetros, opciones y banderas pasados a un ejecutable en C desde una terminal (CLI). 

El programa permite experimentar con tres metodologías clásicas de programación en sistemas operativos.

---

## Métodos Demostrados

1.  **Parseo Manual (`manual`)**:
    *   Itera directamente sobre el arreglo `argv`.
    *   Compara cada elemento de manera explícita usando la función `strcmp()`.
    *   Ideal para entender el funcionamiento interno antes de usar bibliotecas de mayor nivel.
2.  **POSIX `getopt(3)` (`getopt`)**:
    *   Usa la llamada estándar de la biblioteca C `getopt()` para opciones de un único carácter (banderas cortas, ej. `-p 8080`).
    *   Gestiona automáticamente variables del sistema como `optarg`, `optind`, y `optopt`.
3.  **GNU `getopt_long(3)` (`getopt_long`)**:
    *   Utiliza la extensión de GNU `getopt_long()` para soportar banderas de formato largo (legibles para humanos, ej. `--port 8080`), que son el estándar en aplicaciones modernas.

---

## Compilación

Compila el proyecto ejecutando `make` en esta carpeta:
```bash
make
```
Esto creará el binario `cli_parser`. Para limpiar la compilación, usa `make clean`.

---

## Ejemplos de Ejecución

Cada modo muestra primero un **trazado en color gris** del contenido exacto del vector `argv` cargado en el stack de memoria al iniciarse, y luego el resultado de la configuración parseada.

### 1. Probar Parseo Manual
```bash
./cli_parser manual -n "Edison" -p 8080 -v
```

### 2. Probar POSIX getopt
```bash
./cli_parser getopt -n "Maria" -p 9000
```
*(Prueba a omitir el argumento de `-p` escribiendo `./cli_parser getopt -p` y observa el error capturado por getopt).*

### 3. Probar GNU getopt_long
```bash
./cli_parser getopt_long --name "Edison" --port 5000 --verbose
```
*(Puedes mezclar opciones cortas y largas en este modo, ej. `./cli_parser getopt_long -n "Edison" --port 5000 -v`).*
