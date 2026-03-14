# Sensio pH — Medidor de pH con ESP32

Sistema embebido para medir, calibrar y registrar valores de pH en tiempo real. El ESP32 crea su propia red WiFi y sirve una interfaz web accesible desde cualquier teléfono o computadora, sin necesidad de router ni internet.

---

## Hardware necesario

| Componente | Detalle |
|---|---|
| ESP32 | Cualquier módulo (DevKit, Wemos, etc.) |
| ADS1115 | ADC externo de 16 bits por I²C |
| Electrodo de pH | Con módulo acondicionador de señal analógica |
| Cables y protoboard | Conexiones I²C + señal analógica |
| Soluciones buffer | pH 4, pH 7 y pH 10 (para calibración) |

### Diagrama de conexiones

```
ESP32                ADS1115
──────               ───────
3.3V  ───────────►  VDD
GND   ───────────►  GND
GPIO21 (SDA) ────►  SDA
GPIO22 (SCL) ────►  SCL
              ┌──►  ADDR → GND  (dirección I²C: 0x48)

Módulo pH
─────────
Salida analógica ──► A0 del ADS1115
GND ─────────────►  GND

GPIO34 del ESP32 también se lee como respaldo analógico,
pero la fuente principal de datos es el ADS1115.
```

> **Nota:** El pin ADDR del ADS1115 conectado a GND configura la dirección I²C en `0x48`. Si usas otra dirección, cambia `ads.begin(0x48)` en `leerph.h`.

---

## Estructura de archivos

```
phesp32logger/
├── phesp32logger.ino   # Punto de entrada: setup(), loop() y variables globales
├── leerph.h            # Lectura del sensor, filtro y calibración
├── WebHandlers.h       # Servidor web, HTML, API REST y WebSocket
├── DataLoggerWS.h      # Sistema de logs en LittleFS (archivos CSV)
└── DataLogger.h        # Tipos base del logger
```

---

## Cómo funciona el sistema

### 1. Arranque

Al encender el ESP32 ocurre lo siguiente en orden:

1. Se cargan los valores de calibración guardados en la memoria NVS (no volátil). Si es la primera vez, se usan valores por defecto.
2. Se inicializa el ADS1115 por I²C con ganancia `GAIN_TWOTHIRDS` (rango ±6.144 V).
3. Se monta el sistema de archivos LittleFS y se crean dos logs CSV: `usuarios` y `mediciones`.
4. El ESP32 levanta un **Access Point WiFi** con el nombre `Sensio - pH` (sin contraseña).
5. Se activa un **DNS server** que redirige cualquier dominio a la IP del dispositivo (`192.168.4.1`), funcionando como portal cautivo: al conectarse a la red, el teléfono abre automáticamente la interfaz.
6. Se inicia el servidor web en el puerto 80 y el sistema OTA en `/update`.

### 2. Lectura del sensor (`leerph.h`)

Cada **100 ms** se ejecuta el ciclo de lectura:

```
Señal analógica del electrodo
        │
        ▼
   analogRead(34)  ←── lectura cruda del ADC
        │
        ▼
  Filtro exponencial (α = 0.05)
  valor_filtrado = 0.05 × nuevo + 0.95 × anterior
        │
        ▼
  Interpolación lineal por tramos
  usando puntos de calibración pH4, pH7, pH10
        │
        ▼
  constrain(resultado, 0.0, 14.0)
        │
        ▼
     pHvalue  →  enviado por WebSocket cada 200 ms
```

**¿Por qué filtro exponencial?** La señal del electrodo es ruidosa. El filtro promedia lecturas recientes dando más peso al historial que al valor instantáneo. Con α=0.05, la respuesta es lenta pero muy estable.

### 3. Cálculo del pH

Se usan **tres puntos de calibración** (pH 4, 7 y 10) para una interpolación lineal por tramos. Esto es más preciso que una línea recta única porque corrige la no-linealidad del electrodo.

El sistema detecta automáticamente si tu sensor tiene **relación directa** (mayor voltaje = mayor pH) o **relación inversa** (mayor voltaje = menor pH), comparando los valores ADC de los tres puntos.

```
Relación directa:   ADC_pH4 < ADC_pH7 < ADC_pH10
Relación inversa:   ADC_pH4 > ADC_pH7 > ADC_pH10
```

La fórmula en ambos casos:

```
Si la lectura está entre pH 4 y pH 7:
  pH = 7 + (lectura - ADC_pH7) × 3 / (ADC_pH7 - ADC_pH4)

Si la lectura está entre pH 7 y pH 10:
  pH = 7 + (lectura - ADC_pH7) × 3 / (ADC_pH10 - ADC_pH7)
```

### 4. Interfaz web y WebSocket

La página web se actualiza **cada 200 ms** sin recargar, usando WebSocket. El ESP32 envía un JSON con:

```json
{
  "sensorValue": 312,
  "pHValue": 6.85,
  "phColor": "#3fb950",
  "phLabel": "NEUTRO",
  "slope47": 0.0231,
  "slope710": 0.0198,
  "userName": "Juan",
  "userReg": true,
  "uptime": 3742
}
```

El color y la etiqueta del pH cambian dinámicamente en pantalla:

| Rango | Color | Etiqueta |
|---|---|---|
| < 3.0 | Rojo | MUY ACIDO |
| 3.0 – 5.0 | Naranja | ACIDO |
| 5.0 – 6.5 | Amarillo | LIGERAMENTE ACIDO |
| 6.5 – 7.5 | Verde | NEUTRO |
| 7.5 – 9.0 | Azul | BASICO |
| 9.0 – 11.0 | Violeta | MUY BASICO |
| > 11.0 | Rojo | EXTREMO |

### 5. Sistema de logs (`DataLoggerWS.h`)

Los datos se guardan en archivos CSV dentro del LittleFS del ESP32. Hay dos tablas:

**`usuarios_001.csv`**
```
timestamp,nombre,mac_address
12/05/25 09:30,Juan,A1:B2:C3:D4:E5:F6
```

**`mediciones_001.csv`**
```
timestamp,pH,pendiente_4_7,pendiente_7_10,id_usuario
12/05/25 09:31,6.85,0.0231,0.0198,Juan
```

Capacidad máxima: 100 usuarios, 500 mediciones. Cuando se llena, el log rota automáticamente creando un nuevo archivo con índice incrementado (`_002`, `_003`, etc.).

---

## Instalación y compilación

### Dependencias (instalar desde Library Manager)

| Librería | Autor |
|---|---|
| `Adafruit ADS1X15` | Adafruit |
| `ESPAsyncWebServer` | Me-No-Dev |
| `AsyncTCP` | Me-No-Dev |
| `ElegantOTA` | Ayush Sharma |

### Configuración del IDE

- **Board:** ESP32 Dev Module (o el módulo que uses)
- **Partition Scheme:** `Default 4MB with littlefs` ← necesario para que exista LittleFS
- **Upload Speed:** 115200 o 921600

### Pasos

1. Descarga los 4 archivos del proyecto en una carpeta llamada exactamente `phesp32logger`
2. Abre `phesp32logger.ino` en el Arduino IDE
3. Instala las 4 librerías listadas arriba
4. Conecta el ESP32 por USB
5. Selecciona el puerto correcto en **Herramientas → Puerto**
6. Compila y sube (`Ctrl+U`)

---

## Uso paso a paso

### Conexión inicial

1. Enciende el ESP32
2. En tu teléfono o computadora, busca la red WiFi **`Sensio - pH`**
3. Conéctate (sin contraseña)
4. La interfaz debe abrirse automáticamente como portal cautivo. Si no, abre el navegador y entra a **`192.168.4.1`**

### Registro de usuario

Antes de guardar mediciones necesitas registrarte:

1. En la sección **Usuario**, ingresa tu nombre
2. Presiona **REGISTRAR**
3. El banner cambia a verde con tu nombre — ya puedes guardar mediciones

El sistema identifica tu dispositivo mediante una huella digital del navegador (canvas fingerprint), por lo que la próxima vez que te conectes desde el mismo teléfono o computadora te reconocerá automáticamente.

### Calibración del sensor

> Necesitas soluciones buffer de pH 4, pH 7 y pH 10. Se consiguen en tiendas de acuarios, química o laboratorio.

**Proceso recomendado:**

1. Lava el electrodo con agua destilada
2. Sumerge el electrodo en la solución **pH 4**
3. Espera 1–2 minutos hasta que el valor ADC en pantalla se estabilice (deja de variar)
4. Presiona el botón **pH 4** en la sección Calibración
5. El tile mostrará el valor ADC que quedó guardado
6. Saca el electrodo, **lávalo con agua destilada** y sécalo suavemente
7. Repite el proceso con **pH 7** y luego con **pH 10**

Después de calibrar los tres puntos, el sistema detecta automáticamente si la relación es directa o inversa y los cálculos de pH serán correctos.

> **Tip:** Calibra siempre los tres puntos. Con solo dos, el sistema funciona pero pierde precisión en los extremos del rango.

> **Frecuencia de calibración recomendada:** Los electrodos de pH derivan con el tiempo. Recalibra antes de sesiones de medición importantes, o al menos una vez por semana en uso continuo.

### Guardar una medición

1. Con el usuario registrado y el sensor calibrado, el botón **REGISTRAR MEDICION** está activo
2. Sumerge el electrodo en la muestra que quieres analizar
3. Espera a que el valor de pH en pantalla se estabilice
4. Presiona **REGISTRAR MEDICION**
5. Aparece un resumen con pH, pendientes de calibración, usuario y timestamp

### Ver y descargar los datos

- Presiona **ADMINISTRAR LOGS** para acceder a la interfaz del DataLogger
- Desde ahí puedes ver los registros en tabla y descargarlos como CSV
- Los archivos CSV se pueden abrir directamente en Excel o Google Sheets

---

## OTA — Actualización de firmware por WiFi

Una vez que el dispositivo está desplegado no es necesario conectarlo por USB para actualizar el código.

1. Conecta tu computadora a la red `Sensio - pH`
2. En el Arduino IDE, ve a **Sketch → Exportar binario compilado** para obtener el archivo `.bin`
3. En el navegador entra a **`192.168.4.1/update`**, o presiona el botón **ACTUALIZAR FIRMWARE** en la interfaz
4. Arrastra o selecciona el archivo `.bin`
5. El ESP32 se reinicia automáticamente con el nuevo firmware

---

## Rutas de la API

| Ruta | Método | Descripción |
|---|---|---|
| `/` | GET | Página principal |
| `/calibrar4` | GET | Guarda calibración pH 4 con la lectura ADC actual |
| `/calibrar7` | GET | Guarda calibración pH 7 con la lectura ADC actual |
| `/calibrar10` | GET | Guarda calibración pH 10 con la lectura ADC actual |
| `/api/check-user?mac=XX` | GET | Verifica si un usuario ya está registrado |
| `/api/register-user?name=X&mac=XX` | GET | Registra un nuevo usuario |
| `/api/save-measurement` | GET | Guarda la medición actual del usuario activo |
| `/api/get-slopes` | GET | Devuelve las pendientes de calibración actuales |
| `/logger/*` | GET | Interfaz del sistema de logs (DataLoggerWS) |
| `/update` | GET | Interfaz OTA para subir firmware |
| `/ws` | WebSocket | Stream de datos en tiempo real (200 ms) |

---

## Solución de problemas

**El ADS1115 no se inicializa**
Verifica las conexiones I²C (SDA → GPIO21, SCL → GPIO22) y que el pin ADDR esté conectado a GND. El monitor serial a 115200 baud debe mostrar si el begin falló.

**El pH siempre muestra 7.00**
Significa que los denominadores de la calibración son cero — los tres puntos ADC guardados son iguales. Recalibra con los electrodos correctamente sumergidos en cada buffer.

**La página no se abre automáticamente**
El portal cautivo no funciona igual en todos los sistemas operativos. Abre el navegador manualmente y entra directamente a `192.168.4.1`.

**Error "Hora no sincronizada" al guardar**
La interfaz sincroniza la hora al cargarse desde el navegador. Asegúrate de abrir la página principal antes de intentar registrar mediciones.

**La lectura de pH es muy inestable**
Aumenta el tiempo de espera después de sumergir el electrodo. Los electrodos de pH tardan en equilibrarse, especialmente al cambiar de solución. También verifica que todas las conexiones eléctricas sean firmes y que el módulo acondicionador tenga su propia alimentación estable.
