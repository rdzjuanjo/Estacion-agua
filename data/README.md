# Data Folder - LittleFS Files

Esta carpeta contiene archivos que se suben al filesystem LittleFS del ESP32.

## Archivos incluidos:
- `chart.umd.js` - Librería Chart.js para gráficos en tiempo real

## Cómo subir archivos al ESP32:

### Opción 1: Subir solo el filesystem 
```bash
pio run --target uploadfs
```

### Opción 2: Subir firmware + filesystem
```bash
pio run --target upload
pio run --target uploadfs
```

### Opción 3: Desde VS Code
1. Ctrl+Shift+P
2. Buscar "PlatformIO: Upload Filesystem Image"
3. Seleccionar el comando

## Notas:
- Los archivos de esta carpeta se copian al filesystem LittleFS del ESP32
- El ESP32 puede servir estos archivos vía HTTP 
- Chart.js se sirve en la ruta: `http://192.168.4.1/chart.umd.js`
- Máximo ~1-3MB dependiendo de la partición configurada