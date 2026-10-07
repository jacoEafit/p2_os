# El Concepto de Inodo (Index Node)

En sistemas de archivos basados en UNIX/Linux, un **Inodo** (nodo índice) es una estructura de datos que almacena toda la información vital sobre un archivo, *excepto* su nombre.

## ¿Qué contiene exactamente un Inodo?
Cada inodo tiene un número de identificación único y almacena metadatos como:
- **Tipo de archivo** (archivo regular, directorio, etc.)
- **Permisos** (lectura, escritura, ejecución)
- **Propietario y grupo** (UID, GID)
- **Tamaño del archivo** (en bytes)
- **Marcas de tiempo** (fecha de creación, acceso y modificación)
- **Punteros a los bloques físicos de datos** en el disco duro donde realmente está el contenido del archivo.

## Nombres de archivo vs Inodos
Cuando creamos un archivo (por ejemplo, `documento.txt`), el sistema operativo internamente crea dos cosas:
1. El **Inodo** (los metadatos y el bloque de datos físicos en el disco).
2. Una entrada en el directorio (el nombre `documento.txt` que apunta a ese número de Inodo).

El nombre de un archivo es, fundamentalmente, solo una etiqueta apuntando hacia un Inodo.

## ¿Cómo funciona el programa cloner? (Hard Links)
Nuestro programa `cloner` utiliza la System Call `link()` para crear lo que se conoce como un **Hard Link** (enlace duro). 

Cuando le pedimos al sistema crear `documento.clone` a partir de `documento.txt`, el Kernel *no* hace una copia nueva de los datos. En lugar de eso:
1. Toma el Inodo del archivo original.
2. Crea una nueva etiqueta (nombre) en el directorio llamada `documento.clone`.
3. Hace que esta nueva etiqueta apunte **exactamente al mismo Inodo** original.

### Efectos prácticos:
- El Inodo aumenta su "contador de enlaces" a 2.
- Como `documento.txt` y `documento.clone` apuntan al mismo espacio físico en el disco, al abrir y editar cualquiera de ellos estás modificando **los mismos datos**.
- Si eliminas (`rm`) el archivo `documento.txt`, la información en el disco no se borra. El sistema simplemente elimina esa etiqueta. Los datos seguirán intactos y accesibles a través de `documento.clone`, ya que el Inodo solo se destruye cuando la cantidad de enlaces apuntando a él llega a cero.
