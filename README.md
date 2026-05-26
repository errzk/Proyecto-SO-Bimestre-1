# Procesador de Transacciones en Paralelo

## Informacion Institucional

- **Institucion:** Escuela Politecnica Nacional
- **Carrera:** Ciencia de Datos e Inteligencia Artificial
- **Integrantes:** Ambar Salazar, Erick Paez, Adrian Trujillo, Mauro Valencia, Brenda Veintimilla

---

## 1. Descripcion del Problema

El programa lee un archivo CSV con transacciones de ventas, limpia los datos nulos, los normaliza y compara el rendimiento entre dos formas de ejecucion:

- **Modo Secuencial:** Usa un solo hilo para procesar todas las filas una por una.
- **Modo Paralelo:** Usa 3 hilos que trabajan al mismo tiempo, cada uno procesando un bloque diferente de filas.

### Dataset

- 5000 transacciones de ventas
- Columnas: ID, Monto, Cantidad, Categoria, Region, Descuento
- Algunas celdas estan vacias (valores nulos) y deben ser completadas

---

## 2. Estructuras de Datos

### Transaccion

Esta estructura guarda toda la informacion de una fila del CSV.

typedef struct {
    int    id;
    double monto;
    int    cantidad;
    char   categoria[64];
    char   region[64];
    double descuento;
    int monto_nulo;
    int cantidad_nulo;
    int descuento_nulo;
    int categoria_nula;
    int region_nula;
    double monto_norm;
    double cantidad_norm;
    double descuento_norm;
} Transaccion;

**Que significa cada campo:**

- id, monto, cantidad, categoria, region, descuento: son los datos originales
- monto_nulo, cantidad_nulo, descuento_nulo, categoria_nula, region_nula: son flags que valen 1 si esa celda estaba vacia en el CSV
- monto_norm, cantidad_norm, descuento_norm: son los valores ya normalizados entre 0 y 1

### ArgHilo

Esta estructura es lo que recibe cada hilo cuando se crea.

typedef struct {
    int          id_hilo;
    int          inicio;
    int          fin;
    Transaccion *datos;
    double       tiempo_ms;
} ArgHilo;

**Que significa cada campo:**

- id_hilo: 1, 2 o 3 para identificar que hilo es
- inicio: la primera fila que debe procesar este hilo
- fin: la ultima fila (no incluye esta, o sea procesa hasta fin-1)
- datos: puntero al arreglo de transacciones (todos los hilos ven el mismo arreglo)
- tiempo_ms: cuanto tardo este hilo en procesar su bloque

---

## 3. Explicacion del Codigo

### 3.1 Como se lee el archivo CSV (cargar_csv)

El programa abre el archivo con fopen. La primera linea del CSV contiene los nombres de las columnas, asi que se salta con fgets.

Luego, por cada linea del archivo, hace lo siguiente:

Usa strtok() para dividir la linea en partes, usando la coma como separador. La primera vez se le pasa la linea completa, y las siguientes veces se le pasa NULL para que continue donde quedo.

Para los campos numericos (id, monto, cantidad, descuento), usa atoi() para convertir a entero o atof() para convertir a decimal.

Si un campo esta vacio (strtok devuelve NULL o un string vacio), entonces se marca el flag correspondiente como 1. Por ejemplo, si la celda de monto esta vacia, se pone monto_nulo = 1.

### 3.2 Como se calculan las estadisticas globales (calcular_estadisticas_globales)

Esta funcion se ejecuta UNA SOLA VEZ antes de crear los hilos. Recorre todas las filas y calcula:

**Para las columnas numericas (monto, cantidad, descuento):**

- Acumula la suma de todos los valores no nulos para calcular la media (promedio)
- Lleva registro del valor minimo y maximo

**Para las columnas categoricas (categoria, region):**

- Cuenta cuantas veces aparece cada valor usando una tabla de frecuencias
- El valor que mas aparece es la moda

Estos valores se guardan en variables globales. Todos los hilos los van a leer despues.

### 3.3 El procesamiento de datos (procesar_bloque)

Esta es la funcion mas importante. Es ejecutada por CADA HILO en su rango de filas. Hace lo siguiente para cada fila:

**Paso 1: Completar los valores nulos numericos**

Si el monto estaba vacio (monto_nulo = 1), se pone el valor de la media global de montos.
Si la cantidad estaba vacia, se pone la media global de cantidades redondeada a entero.
Si el descuento estaba vacio, se pone la media global de descuentos.

**Paso 2: Completar los valores nulos categoricos**

Si la categoria estaba vacia, se pone la moda (la categoria que mas aparece en todo el dataset).
Si la region estaba vacia, se pone la moda de regiones.

**Paso 3: Normalizar los valores a escala [0,1]**

La normalizacion usa la formula: (valor - minimo) / (maximo - minimo)

Esto hace que todos los valores queden entre 0 y 1. Por ejemplo, si los montos van de 0 a 5000, un monto de 2500 quedaria en 0.5.

**Paso 4: Carga de trabajo simulada**

Se hacen calculos matematicos (log, sqrt, seno, coseno) dentro de un bucle de 200 iteraciones. Esto simula un procesamiento real (como validaciones de negocio) y hace que se note la diferencia de velocidad entre el modo secuencial y el paralelo.

### 3.4 Como funcionan los hilos (funcion_hilo)

Esta es la funcion que ejecuta cada hilo cuando se crea. Hace cuatro cosas:

1. Mide el tiempo de inicio
2. Llama a procesar_bloque() con el rango de filas que le tocan
3. Mide el tiempo de fin y calcula cuanto tardo
4. Imprime un mensaje diciendo cuantas filas proceso y en cuanto tiempo

**El mutex: que es y para que se usa**

Un mutex (mutual exclusion) es un mecanismo de sincronizacion que permite que solo un hilo acceda a un recurso compartido a la vez. En este programa, se usa un mutex para proteger la funcion printf.

**Por que se necesita el mutex:** Cuando varios hilos ejecutan printf al mismo tiempo, sus mensajes se entremezclan caracter por caracter. Por ejemplo, si el Hilo 1 intenta imprimir "[Hilo 1] filas 0-1665" y el Hilo 2 intenta imprimir "[Hilo 2] filas 1666-3331" al mismo tiempo, el resultado podria ser algo como "[Hilo 1] [Hilo 2] filas filas 0-1665 1666-3331". El mutex evita esto haciendo que los hilos se turnen para imprimir.

**Como funciona:** Cuando un hilo quiere imprimir, llama a pthread_mutex_lock. Si el mutex esta libre, el hilo lo toma y procede a imprimir. Si otro hilo ya lo tiene, el hilo se queda dormido esperando. Cuando termina de imprimir, llama a pthread_mutex_unlock para liberar el mutex, permitiendo que otro hilo lo use.

### 3.5 Como se crean y gestionan los hilos (ejecutar_paralelo)

Esta funcion hace lo siguiente:

1. Calcula cuantas filas le tocan a cada hilo: total_filas dividido por 3
2. Para cada hilo, crea una estructura ArgHilo con su id, su fila de inicio y su fila de fin
3. Crea cada hilo con pthread_create. Esta funcion recibe: un puntero para guardar el ID del hilo, NULL para atributos por defecto, el nombre de la funcion a ejecutar (funcion_hilo), y los argumentos (la estructura ArgHilo)
4. Una vez creados los 3 hilos, el programa llama a pthread_join para cada hilo. pthread_join hace que el programa se quede esperando hasta que el hilo termine
5. Finalmente, retorna el tiempo total que tardo todo el proceso

### 3.6 El modo secuencial (ejecutar_secuencial)

El modo secuencial es mas simple: llama a procesar_bloque() con todas las filas (desde 0 hasta total_filas) y mide cuanto tarda. No usa hilos.

### 3.7 El programa principal (main)

El main ejecuta estas etapas en orden:

1. Carga el archivo CSV llamando a cargar_csv()
2. Calcula las estadisticas globales llamando a calcular_estadisticas_globales()
3. Ejecuta el modo secuencial y guarda el tiempo
4. Ejecuta el modo paralelo y guarda el tiempo
5. Calcula el speedup dividiendo el tiempo secuencial por el tiempo paralelo
6. Muestra una muestra de las primeras 8 filas ya procesadas

---

## 4. Division de las Filas entre los Hilos

Para 5000 filas y 3 hilos, la division queda asi:

- Hilo 1: procesa las filas 0 a 1665 (1666 filas)
- Hilo 2: procesa las filas 1666 a 3331 (1666 filas)
- Hilo 3: procesa las filas 3332 a 4999 (1668 filas)

El ultimo hilo procesa un poco mas de filas porque 5000 no es divisible exactamente por 3.

---

## 5. Ejemplo de Procesamiento de una Fila

**Fila original (con nulos):**

ID: 102
Monto: (vacio)
Cantidad: 0
Categoria: (vacio)
Region: Este
Descuento: 0.253

**Paso 1 - Completar numericos:**

La media de montos es 131.56, entonces monto se convierte en 131.56

**Paso 2 - Completar categoricos:**

La moda de categorias es "Electronica", entonces categoria se convierte en "Electronica"

**Paso 3 - Normalizar:**

El minimo monto es 0.00 y el maximo monto es 4997.29
monto_norm = (131.56 - 0) / (4997.29 - 0) = 0.0263

**Resultado final:**

ID: 102
Monto: 131.56
Cantidad: 0
Categoria: Electronica
Region: Este
Descuento: 0.253
Monto_norm: 0.0263

---

## 6. Resultados

### Speedup (x)

El speedup es la relacion entre el tiempo secuencial y el tiempo paralelo. Indica cuantas veces es mas rapido el modo paralelo.

Speedup = Tiempo_secuencial / Tiempo_paralelo

### Tiempos de ejecucion

| Modo | Tiempo |
|------|--------|
| Secuencial | 131.56 milisegundos |
| Paralelo (3 hilos) | 40.84 milisegundos |

### Speedup obtenido

Speedup = 131.56 / 40.84 = 3.22x

Esto significa que el modo paralelo es 3.22 veces mas rapido que el secuencial.

### Eficiencia

La eficiencia indica que tan bien se estan usando los hilos. Se calcula como:

Eficiencia = (Speedup / Cantidad_de_hilos) x 100

Eficiencia = (3.22 / 3) x 100 = 107.3%

### Conclusion

El procesamiento paralelo con 3 hilos es 3.22 veces mas rapido que el secuencial, con una mejora de aproximadamente el 69%. Esto demuestra que dividir el trabajo entre varios hilos es muy beneficioso para este tipo de procesamiento.

---

## 7. Compilacion y Ejecucion

### Compilar el programa

gcc -o proyecto proyecto.c -lpthread -lm

### Ejecutar el programa

./proyecto transacciones.csv

### Compilar sin optimizacion (para depurar)

gcc -o proyecto proyecto.c -lpthread -lm -O0

---

## 8. Lineas de Codigo Mas Importantes

**La funcion que ejecuta cada hilo:**

void *funcion_hilo(void *arg)

**Crear un hilo:**

pthread_create(&hilos[i], NULL, funcion_hilo, &args[i])

**Esperar a que un hilo termine:**

pthread_join(hilos[i], NULL)

**Inicializar el mutex:**

pthread_mutex_t mutex_print = PTHREAD_MUTEX_INITIALIZER;

**Bloquear y desbloquear el mutex para proteger printf:**

pthread_mutex_lock(&mutex_print);
printf("...");
pthread_mutex_unlock(&mutex_print);

**El bucle que simula trabajo pesado:**

for (int k = 0; k < 200; k++) {
    tmp += sin(tmp + k) * cos(t->descuento + k);
}

**La imputacion de nulos numericos:**

if (t->monto_nulo) t->monto = media_global[COL_MONTO];

**La imputacion de nulos categoricos:**

if (t->categoria_nula) strcpy(t->categoria, moda_cat);

**La normalizacion min-max:**

t->monto_norm = (t->monto - min_global[COL_MONTO]) / (max_global[COL_MONTO] - min_global[COL_MONTO]);

---

## 9. Referencias

- POSIX Threads Programming - Lawrence Livermore National Laboratory
- Amdahl, G. M. (1967). "Validity of the single processor approach"
- IEEE Std 1003.1-2017 - pthread_create, pthread_join, pthread_mutex
