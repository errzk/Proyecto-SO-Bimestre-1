#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <time.h>
 
/* ─── Constantes ─────────────────────────────────────────────── */
#define MAX_FILAS    10000
#define MAX_STR      64
#define NUM_HILOS    3
 
/* Indices de columnas numericas */
#define COL_MONTO      0
#define COL_CANTIDAD   1
#define COL_DESCUENTO  2
#define NUM_COLS_NUM   3
 
/* Indices de columnas categoricas */
#define COL_CATEGORIA  0
#define COL_REGION     1
#define NUM_COLS_CAT   2
 
/* ─── Estructuras ────────────────────────────────────────────── */
 
/* Representa una fila del CSV ya parseada */
typedef struct {
    int    id;
    double monto;
    int    cantidad;
    char   categoria[MAX_STR];
    char   region[MAX_STR];
    double descuento;
 
    /* 1 = el campo estaba vacio en el archivo original */
    int monto_nulo;
    int cantidad_nulo;
    int descuento_nulo;
    int categoria_nula;
    int region_nula;
 
    /* Resultados de la normalizacion min-max */
    double monto_norm;
    double cantidad_norm;
    double descuento_norm;
} Transaccion;
 
/* Datos que recibe cada hilo al momento de su creacion */
typedef struct {
    int          id_hilo;
    int          inicio;    /* primera fila asignada a este hilo */
    int          fin;       /* ultima fila (exclusive) */
    Transaccion *datos;
    int          n_filas;
    double       tiempo_ms; /* cuanto tardo este hilo, se llena al terminar */
} ArgHilo;
 
/* Entrada de tabla de frecuencias para calcular la moda */
typedef struct { char valor[MAX_STR]; int conteo; } EntradaModa;
 
/* ─── Variables globales ─────────────────────────────────────── */
 
Transaccion datos[MAX_FILAS];
int total_filas = 0;
 
/*
 * Mutex que protege la salida por consola cuando varios hilos
 * imprimen al mismo tiempo. Sin esto los mensajes se solaparian.
 */
pthread_mutex_t mutex_print = PTHREAD_MUTEX_INITIALIZER;
 
/* Estadisticas calculadas antes de lanzar los hilos */
double media_global[NUM_COLS_NUM];
double min_global  [NUM_COLS_NUM];
double max_global  [NUM_COLS_NUM];
 
#define MAX_CATEGORIAS 32
EntradaModa tabla_categoria[MAX_CATEGORIAS];
EntradaModa tabla_region   [MAX_CATEGORIAS];
int n_categorias = 0, n_regiones = 0;
 
/* ─── Utilidad de tiempo ─────────────────────────────────────── */
 
/* clock_gettime(MONOTONIC) ofrece mayor precision que clock() */
double tiempo_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
 
/* ─── Limpieza de string ─────────────────────────────────────── */
 
/* Elimina \r, \n y espacios al final de un string */
void trim(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' '))
        s[--len] = '\0';
}
 
/* ─── Carga del CSV ─────────────────────────────────────────── */
 
/*
 * Parsea el archivo linea por linea.
 * Campos vacios quedan marcados con el flag _nulo correspondiente.
 * Retorna numero de filas cargadas, -1 si hay error de apertura.
 */
int cargar_csv(const char *ruta) {
    FILE *f = fopen(ruta, "r");
    if (!f) { perror("fopen"); return -1; }
 
    char linea[512];
    fgets(linea, sizeof(linea), f); /* saltar encabezado */
 
    int n = 0;
    while (fgets(linea, sizeof(linea), f) && n < MAX_FILAS) {
        trim(linea);
        if (linea[0] == '\0') continue;
 
        Transaccion *t = &datos[n];
        memset(t, 0, sizeof(Transaccion));
 
        char buf[512];
        strncpy(buf, linea, sizeof(buf));
 
        char *tok = strtok(buf, ",");
        if (!tok) continue;
        t->id = atoi(tok);
 
        tok = strtok(NULL, ",");
        if (!tok || tok[0] == '\0') t->monto_nulo = 1;
        else t->monto = atof(tok);
 
        tok = strtok(NULL, ",");
        if (!tok || tok[0] == '\0') t->cantidad_nulo = 1;
        else t->cantidad = atoi(tok);
 
        tok = strtok(NULL, ",");
        if (!tok || tok[0] == '\0') t->categoria_nula = 1;
        else { strncpy(t->categoria, tok, MAX_STR-1); trim(t->categoria); }
 
        tok = strtok(NULL, ",");
        if (!tok || tok[0] == '\0') t->region_nula = 1;
        else { strncpy(t->region, tok, MAX_STR-1); trim(t->region); }
 
        tok = strtok(NULL, ",");
        if (!tok || tok[0] == '\0') t->descuento_nulo = 1;
        else { char tmp[32]; strncpy(tmp,tok,31); trim(tmp); t->descuento = atof(tmp); }
 
        n++;
    }
    fclose(f);
    return n;
}
 
/* ─── Estadísticas globales ──────────────────────────────────── */
 
/*
 * Se ejecuta una sola vez antes de crear los hilos.
 * Calcula medias, modas y rangos que luego todos los hilos leen
 * sin modificar (acceso de solo lectura = no necesita mutex).
 */
void calcular_estadisticas_globales(void) {
    double suma[NUM_COLS_NUM] = {0};
    int    cnt [NUM_COLS_NUM] = {0};
 
    for (int c = 0; c < NUM_COLS_NUM; c++) {
        min_global[c] =  1e18;
        max_global[c] = -1e18;
    }
 
    for (int i = 0; i < total_filas; i++) {
        Transaccion *t = &datos[i];
 
        /* Acumular monto */
        if (!t->monto_nulo) {
            suma[COL_MONTO] += t->monto; cnt[COL_MONTO]++;
            if (t->monto < min_global[COL_MONTO]) min_global[COL_MONTO] = t->monto;
            if (t->monto > max_global[COL_MONTO]) max_global[COL_MONTO] = t->monto;
        }
        /* Acumular cantidad */
        if (!t->cantidad_nulo) {
            suma[COL_CANTIDAD] += t->cantidad; cnt[COL_CANTIDAD]++;
            if (t->cantidad < min_global[COL_CANTIDAD]) min_global[COL_CANTIDAD] = t->cantidad;
            if (t->cantidad > max_global[COL_CANTIDAD]) max_global[COL_CANTIDAD] = t->cantidad;
        }
        /* Acumular descuento */
        if (!t->descuento_nulo) {
            suma[COL_DESCUENTO] += t->descuento; cnt[COL_DESCUENTO]++;
            if (t->descuento < min_global[COL_DESCUENTO]) min_global[COL_DESCUENTO] = t->descuento;
            if (t->descuento > max_global[COL_DESCUENTO]) max_global[COL_DESCUENTO] = t->descuento;
        }
 
        /* Frecuencias para moda de categoria */
        if (!t->categoria_nula) {
            int ok = 0;
            for (int k = 0; k < n_categorias; k++) {
                if (strcmp(tabla_categoria[k].valor, t->categoria) == 0) {
                    tabla_categoria[k].conteo++; ok=1; break;
                }
            }
            if (!ok && n_categorias < MAX_CATEGORIAS) {
                strncpy(tabla_categoria[n_categorias].valor, t->categoria, MAX_STR-1);
                tabla_categoria[n_categorias++].conteo = 1;
            }
        }
        /* Frecuencias para moda de region */
        if (!t->region_nula) {
            int ok = 0;
            for (int k = 0; k < n_regiones; k++) {
                if (strcmp(tabla_region[k].valor, t->region) == 0) {
                    tabla_region[k].conteo++; ok=1; break;
                }
            }
            if (!ok && n_regiones < MAX_CATEGORIAS) {
                strncpy(tabla_region[n_regiones].valor, t->region, MAX_STR-1);
                tabla_region[n_regiones++].conteo = 1;
            }
        }
    }
 
    for (int c = 0; c < NUM_COLS_NUM; c++)
        media_global[c] = cnt[c] > 0 ? suma[c] / cnt[c] : 0.0;
}
 
const char *obtener_moda(EntradaModa *tabla, int n) {
    int mx = -1, idx = 0;
    for (int i = 0; i < n; i++)
        if (tabla[i].conteo > mx) { mx = tabla[i].conteo; idx = i; }
    return tabla[idx].valor;
}
 
/* ─── Procesamiento de un bloque ─────────────────────────────── */
 
/*
 * Recibe un rango [inicio, fin) y aplica las tres etapas:
 *   1. Imputacion de nulos numericos con la media.
 *   2. Imputacion de nulos categoricos con la moda.
 *   3. Normalizacion min-max al rango [0,1].
 *
 * Las operaciones son independientes por fila, por eso es seguro
 * dividir el arreglo entre varios hilos sin necesidad de mutex aqui.
 */
void procesar_bloque(int inicio, int fin) {
    const char *moda_cat = obtener_moda(tabla_categoria, n_categorias);
    const char *moda_reg = obtener_moda(tabla_region,    n_regiones);
 
    double r_monto  = max_global[COL_MONTO]    - min_global[COL_MONTO];
    double r_cant   = max_global[COL_CANTIDAD]  - min_global[COL_CANTIDAD];
    double r_desc   = max_global[COL_DESCUENTO] - min_global[COL_DESCUENTO];
 
    for (int i = inicio; i < fin; i++) {
        Transaccion *t = &datos[i];
 
        /* Etapa 1: reemplazar nulos numericos con la media global */
        if (t->monto_nulo)     t->monto    = media_global[COL_MONTO];
        if (t->cantidad_nulo)  t->cantidad = (int)round(media_global[COL_CANTIDAD]);
        if (t->descuento_nulo) t->descuento = media_global[COL_DESCUENTO];
 
        /* Etapa 2: reemplazar nulos categoricos con la moda global */
        if (t->categoria_nula) strncpy(t->categoria, moda_cat, MAX_STR-1);
        if (t->region_nula)    strncpy(t->region,    moda_reg, MAX_STR-1);
 
        /* Etapa 3: normalizacion min-max */
        t->monto_norm    = r_monto > 0 ? (t->monto    - min_global[COL_MONTO])    / r_monto : 0.0;
        t->cantidad_norm = r_cant  > 0 ? (t->cantidad - min_global[COL_CANTIDAD])  / r_cant  : 0.0;
        t->descuento_norm= r_desc  > 0 ? (t->descuento- min_global[COL_DESCUENTO]) / r_desc  : 0.0;
 
        /*
         * Carga de trabajo adicional: calculos matematicos que simulan
         * validaciones mas complejas (log, sqrt, pow) y hacen visible
         * el speedup entre modos. En un caso real esto seria validacion
         * de reglas de negocio, deteccion de outliers, etc.
         */
        volatile double tmp = log(t->monto + 1.0) * sqrt(t->cantidad + 1.0);
        for (int k = 0; k < 200; k++) tmp += sin(tmp + k) * cos(t->descuento + k);
        (void)tmp; /* evitar warning de variable no usada */
    }
}
 
/* ─── Función que ejecuta cada hilo ─────────────────────────── */
 
/*
 * pthread_create espera una funcion con firma void* f(void*).
 * Cada hilo recibe su ArgHilo, procesa su bloque y registra el tiempo.
 */
void *funcion_hilo(void *arg) {
    ArgHilo *a = (ArgHilo *)arg;
 
    double t0 = tiempo_ms();
    procesar_bloque(a->inicio, a->fin);
    a->tiempo_ms = tiempo_ms() - t0;
 
    /* mutex para que los prints no se mezclen entre hilos */
    pthread_mutex_lock(&mutex_print);
    printf("  [Hilo %d] filas %5d - %5d procesadas en %7.2f ms\n",
           a->id_hilo, a->inicio, a->fin - 1, a->tiempo_ms);
    pthread_mutex_unlock(&mutex_print);
 
    return NULL;
}
 
/* ─── Modo paralelo ─────────────────────────────────────────── */
 
/*
 * Divide el trabajo en NUM_HILOS partes iguales, crea los hilos
 * con pthread_create y espera su terminacion con pthread_join.
 * Retorna el tiempo de pared (wall time) total.
 */
double ejecutar_paralelo(void) {
    pthread_t hilos[NUM_HILOS];
    ArgHilo   args [NUM_HILOS];
    int bloque = total_filas / NUM_HILOS;
 
    double t0 = tiempo_ms();
 
    for (int i = 0; i < NUM_HILOS; i++) {
        args[i].id_hilo   = i + 1;
        args[i].inicio    = i * bloque;
        /* el ultimo hilo toma las filas restantes si no divide exacto */
        args[i].fin       = (i == NUM_HILOS - 1) ? total_filas : (i+1)*bloque;
        args[i].datos     = datos;
        args[i].n_filas   = total_filas;
        args[i].tiempo_ms = 0.0;
 
        if (pthread_create(&hilos[i], NULL, funcion_hilo, &args[i]) != 0) {
            perror("pthread_create"); exit(EXIT_FAILURE);
        }
    }
 
    /* Barrera: main espera a que todos los hilos terminen */
    for (int i = 0; i < NUM_HILOS; i++)
        pthread_join(hilos[i], NULL);
 
    return tiempo_ms() - t0;
}
 
/* ─── Modo secuencial ───────────────────────────────────────── */
 
/*
 * Todo el procesamiento ocurre en el hilo principal.
 * Es la linea base con la que se compara el modo paralelo.
 */
double ejecutar_secuencial(void) {
    double t0 = tiempo_ms();
    procesar_bloque(0, total_filas);
    return tiempo_ms() - t0;
}
 
/* ─── Muestra de resultados ──────────────────────────────────── */
 
void mostrar_muestra(int n) {
    printf("\n%-5s %-10s %-6s %-12s %-8s %-9s | %-7s %-7s %-7s\n",
           "ID","Monto","Cant","Categoria","Region","Descuento",
           "M_norm","C_norm","D_norm");
    printf("%s\n", "-----+----------+------+------------+--------+---------+---------+-------+-------");
    for (int i = 0; i < n && i < total_filas; i++) {
        Transaccion *t = &datos[i];
        printf("%-5d %-10.2f %-6d %-12s %-8s %-9.3f | %-7.4f %-7.4f %-7.4f\n",
               t->id, t->monto, t->cantidad, t->categoria, t->region,
               t->descuento, t->monto_norm, t->cantidad_norm, t->descuento_norm);
    }
}
 
/* ─── Main ──────────────────────────────────────────────────── */
 
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <archivo.csv>\n", argv[0]);
        return EXIT_FAILURE;
    }
 
    printf("=== Procesador de Transacciones - EPN Sistemas Operativos ===\n\n");
 
    /* Paso 1: cargar el archivo en memoria */
    printf("[1] Cargando '%s'...\n", argv[1]);
    total_filas = cargar_csv(argv[1]);
    if (total_filas < 0) return EXIT_FAILURE;
    printf("    %d filas cargadas.\n\n", total_filas);
 
    /* Paso 2: calcular estadisticas antes de paralelizar */
    printf("[2] Calculando estadisticas globales...\n");
    calcular_estadisticas_globales();
    printf("    Media  -> monto: %.2f | cantidad: %.1f | descuento: %.3f\n",
           media_global[COL_MONTO], media_global[COL_CANTIDAD], media_global[COL_DESCUENTO]);
    printf("    Moda   -> categoria: '%s' | region: '%s'\n",
           obtener_moda(tabla_categoria, n_categorias),
           obtener_moda(tabla_region, n_regiones));
    printf("    Rango monto: [%.2f, %.2f]\n\n",
           min_global[COL_MONTO], max_global[COL_MONTO]);
 
    /* Paso 3: modo secuencial (linea base) */
    printf("[3] MODO SECUENCIAL (1 hilo)...\n");
    double t_sec = ejecutar_secuencial();
    printf("    Tiempo total secuencial: %.2f ms\n\n", t_sec);
 
    /* Paso 4: modo paralelo */
    printf("[4] MODO PARALELO (%d hilos)...\n", NUM_HILOS);
    double t_par = ejecutar_paralelo();
    printf("    Tiempo total paralelo  : %.2f ms\n\n", t_par);
 
    /* Paso 5: metricas de rendimiento */
    double speedup    = t_sec / t_par;
    double eficiencia = speedup / NUM_HILOS * 100.0;
 
    printf("========================================\n");
    printf("  Tiempo secuencial : %8.2f ms\n", t_sec);
    printf("  Tiempo paralelo   : %8.2f ms\n", t_par);
    printf("  Speedup           : %8.2fx\n",   speedup);
    printf("  Eficiencia        : %7.1f %%\n",  eficiencia);
    printf("  Hilos usados      : %8d\n",      NUM_HILOS);
    printf("  Filas procesadas  : %8d\n",      total_filas);
    printf("========================================\n");
 
    /* Paso 6: muestra de datos procesados */
    printf("\n[5] Muestra de las primeras 8 filas procesadas:\n");
    mostrar_muestra(8);
 
    pthread_mutex_destroy(&mutex_print);
    printf("\n[OK] Fin del programa.\n");
    return EXIT_SUCCESS;
}
