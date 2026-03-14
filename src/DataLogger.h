#ifndef DATALOGGER_H
#define DATALOGGER_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <time.h>

// ─── Límites ────────────────────────────────────────────────────────────────
#define MAX_LOGS         10
#define MAX_COLUMNS      20
#define MAX_LOG_NAME     32
#define MAX_COLUMN_NAME  32
#define MAX_READ_BUFFER  512

// ─── Enumeraciones ──────────────────────────────────────────────────────────
enum ColumnType   { TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_STRING, TYPE_TIMESTAMP, TYPE_NTP_TIMESTAMP };
enum TimeUnit     { MS, SECONDS, MINUTES, HOURS, DAYS };
enum FileSystemType { FS_LITTLEFS, FS_SD };

// ─── Estructuras ─────────────────────────────────────────────────────────────
struct Column {
  char       name[MAX_COLUMN_NAME];
  ColumnType type;
  void*      varPtr;
  int        decimals;
  TimeUnit   timeUnit;
  char       timeFormat[32];
};

struct LogFile {
  char     name[MAX_LOG_NAME];
  Column   columns[MAX_COLUMNS];
  uint8_t  columnCount;
  uint32_t maxEntries;
  uint32_t currentEntries;
  uint16_t fileIndex;
  bool     hasHeader;
};

typedef void (*RotationCallback)(const char* logName, uint16_t newIndex);
typedef void (*SpaceFullCallback)(const char* logName);

// ─── Clase ───────────────────────────────────────────────────────────────────
class DataLogger {
public:
  DataLogger() {
    fileSystem        = nullptr;
    fsType            = FS_LITTLEFS;
    logCount          = 0;
    maxFileSizeBytes  = 0;
    rotationCallback  = nullptr;
    spaceFullCallback = nullptr;
    maxEntriesDefault = 1000;
    memset(logs, 0, sizeof(logs));
  }

  // ── Inicialización ─────────────────────────────────────────────────────────
  bool begin(FileSystemType type) {
    fsType = type;
    if (fsType == FS_LITTLEFS) {
      if (!LittleFS.begin(true)) { Serial.println("LittleFS: Error al inicializar"); return false; }
      fileSystem = &LittleFS;
      Serial.println("LittleFS: Inicializado correctamente");
    } else {
      if (!SD.begin()) { Serial.println("SD: Error al inicializar"); return false; }
      fileSystem = &SD;
      Serial.println("SD: Inicializado correctamente");
    }
    return true;
  }

  void end() {
    if (fsType == FS_LITTLEFS) LittleFS.end();
    else if (fsType == FS_SD) SD.end();
    logCount = 0;
    memset(logs, 0, sizeof(logs));
  }

  // ── Creación de logs ───────────────────────────────────────────────────────
  bool createLog(const char* logName) {
    if (logCount >= MAX_LOGS)               { Serial.println("Error: Máximo de logs alcanzado");   return false; }
    if (strlen(logName) >= MAX_LOG_NAME)    { Serial.println("Error: Nombre de log muy largo");    return false; }
    if (findLog(logName) != nullptr)        { Serial.println("Error: Log ya existe");               return false; }

    LogFile* l = &logs[logCount];
    strncpy(l->name, logName, MAX_LOG_NAME - 1);
    l->name[MAX_LOG_NAME - 1] = '\0';
    l->columnCount   = 0;
    l->maxEntries    = 0;
    l->currentEntries= 0;
    l->fileIndex     = 1;
    l->hasHeader     = false;
    logCount++;
    Serial.printf("Log creado: %s\n", logName);
    return true;
  }

  // ── Agregar columnas ───────────────────────────────────────────────────────
  bool addIntColumn(const char* logName, const char* colName, int* ptr) {
    return _addColumn(logName, colName, TYPE_INT, ptr, 0, SECONDS, "%Y-%m-%d %H:%M:%S");
  }
  bool addFloatColumn(const char* logName, const char* colName, float* ptr, int decimals = 2) {
    return _addColumn(logName, colName, TYPE_FLOAT, ptr, decimals, SECONDS, "%Y-%m-%d %H:%M:%S");
  }
  bool addBoolColumn(const char* logName, const char* colName, bool* ptr) {
    return _addColumn(logName, colName, TYPE_BOOL, ptr, 0, SECONDS, "%Y-%m-%d %H:%M:%S");
  }
  bool addStringColumn(const char* logName, const char* colName, String* ptr) {
    return _addColumn(logName, colName, TYPE_STRING, ptr, 0, SECONDS, "%Y-%m-%d %H:%M:%S");
  }
  bool addTimestamp(const char* logName, const char* colName, TimeUnit unit = SECONDS) {
    return _addColumn(logName, colName, TYPE_TIMESTAMP, nullptr, 0, unit, "%Y-%m-%d %H:%M:%S");
  }
  bool addNTPTimestamp(const char* logName, const char* colName, const char* format = "%Y-%m-%d %H:%M:%S") {
    return _addColumn(logName, colName, TYPE_NTP_TIMESTAMP, nullptr, 0, SECONDS, format);
  }

  // ── Configuración ──────────────────────────────────────────────────────────
  void setMaxEntries(const char* logName, uint32_t maxEntries) {
    LogFile* l = findLog(logName);
    if (l) l->maxEntries = maxEntries;
  }
  void setMaxFileSize(uint32_t maxSizeKB) { maxFileSizeBytes = maxSizeKB * 1024; }
  void onRotation(RotationCallback cb)    { rotationCallback  = cb; }
  void onSpaceFull(SpaceFullCallback cb)  { spaceFullCallback = cb; }

  // ── Logging ────────────────────────────────────────────────────────────────
  bool log(const char* logName) {
    LogFile* l = findLog(logName);
    if (!l || l->columnCount == 0) { Serial.println("Error: Log no encontrado o sin columnas"); return false; }

    if (!checkAndFreeSpace()) {
      Serial.println("Error: Sin espacio disponible");
      if (spaceFullCallback) spaceFullCallback(logName);
      return false;
    }

    char filename[64];
    getCurrentFileName(logName, filename, sizeof(filename));

    if (!l->hasHeader) {
      if (!writeHeader(l)) { Serial.println("Error: No se pudo escribir el header"); return false; }
      l->hasHeader = true;
    }

    // Rotar por tamaño
    if (maxFileSizeBytes > 0 && getFileSize(filename) >= maxFileSizeBytes) {
      Serial.println("Rotando archivo por tamaño...");
      if (!rotateFile(l)) return false;
      getCurrentFileName(logName, filename, sizeof(filename));
    }

    if (!writeEntry(l)) { Serial.println("Error: No se pudo escribir la entrada"); return false; }
    l->currentEntries++;

    // Rotar por número de entradas
    if (l->maxEntries > 0 && l->currentEntries >= l->maxEntries) {
      Serial.println("Rotando archivo por entradas...");
      if (!rotateFile(l)) return false;
    }
    return true;
  }

  // ── Lectura ────────────────────────────────────────────────────────────────
  // FIX: el índice 0 es la primera entrada de datos (la línea 1, justo después del header)
  bool readEntry(const char* logName, uint32_t index, char* buffer, size_t bufferSize) {
    LogFile* l = findLog(logName);
    if (!l) return false;

    char filename[64];
    getCurrentFileName(logName, filename, sizeof(filename));

    File file = fileSystem->open(filename, "r");
    if (!file) return false;

    // Saltar header
    if (file.available()) file.readStringUntil('\n');

    uint32_t currentLine = 0;
    bool found = false;

    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();  // quitar \r sobrante en Windows
      if (line.length() == 0) continue;
      if (currentLine == index) {
        strncpy(buffer, line.c_str(), bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        found = true;
        break;
      }
      currentLine++;
    }

    file.close();
    return found;
  }

  uint32_t getEntryCount(const char* logName) {
    LogFile* l = findLog(logName);
    if (!l) return 0;
    char filename[64];
    getCurrentFileName(logName, filename, sizeof(filename));
    uint32_t lines = countLinesInFile(filename);
    return lines > 0 ? lines - 1 : 0;  // -1 para el header
  }

  // ── Gestión de archivos ────────────────────────────────────────────────────
  bool clearLog(const char* logName) {
    LogFile* l = findLog(logName);
    if (!l) return false;

    char filename[64];
    for (uint16_t i = 1; i <= l->fileIndex; i++) {
      getFileName(logName, i, filename, sizeof(filename));
      if (fileSystem->exists(filename)) {
        fileSystem->remove(filename);
        Serial.printf("Eliminado: %s\n", filename);
      }
    }
    l->fileIndex      = 1;
    l->currentEntries = 0;
    l->hasHeader      = false;
    Serial.printf("Log limpiado: %s\n", logName);
    return true;
  }

  bool deleteLog(const char* logName) {
    LogFile* l = findLog(logName);
    if (!l) return false;
    clearLog(logName);

    int idx = -1;
    for (uint8_t i = 0; i < logCount; i++) {
      if (strcmp(logs[i].name, logName) == 0) { idx = i; break; }
    }
    if (idx == -1) return false;

    for (uint8_t i = idx; i < logCount - 1; i++) memcpy(&logs[i], &logs[i + 1], sizeof(LogFile));
    logCount--;
    memset(&logs[logCount], 0, sizeof(LogFile));
    Serial.printf("Log eliminado: %s\n", logName);
    return true;
  }

  uint8_t getLogCount() { return logCount; }

  bool getLogName(uint8_t index, char* buffer, size_t bufferSize) {
    if (index >= logCount) return false;
    strncpy(buffer, logs[index].name, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
  }

  // ── Información del sistema ────────────────────────────────────────────────
  uint32_t getFreeSpace() {
    if (!fileSystem) return 0;
    return (fsType == FS_LITTLEFS) ? LittleFS.totalBytes() - LittleFS.usedBytes()
                                   : SD.totalBytes() - SD.usedBytes();
  }
  uint32_t getTotalSpace() {
    if (!fileSystem) return 0;
    return (fsType == FS_LITTLEFS) ? LittleFS.totalBytes() : SD.totalBytes();
  }
  float getSpaceUsagePercent() {
    uint32_t total = getTotalSpace();
    if (total == 0) return 0;
    return ((float)(total - getFreeSpace()) / total) * 100.0f;
  }

protected:
  fs::FS*         fileSystem;
  FileSystemType  fsType;
  LogFile         logs[MAX_LOGS];
  uint8_t         logCount;
  uint32_t        maxFileSizeBytes;
  uint32_t        maxEntriesDefault;
  RotationCallback  rotationCallback;
  SpaceFullCallback spaceFullCallback;

  // ── Helpers internos ───────────────────────────────────────────────────────
  LogFile* findLog(const char* logName) {
    for (uint8_t i = 0; i < logCount; i++)
      if (strcmp(logs[i].name, logName) == 0) return &logs[i];
    return nullptr;
  }

  void getFileName(const char* logName, uint16_t index, char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "/%s_%03d.csv", logName, index);
  }

  void getCurrentFileName(const char* logName, char* buffer, size_t bufferSize) {
    LogFile* l = findLog(logName);
    if (!l) { buffer[0] = '\0'; return; }
    getFileName(logName, l->fileIndex, buffer, bufferSize);
  }

  bool writeHeader(LogFile* l) {
    if (!l || !fileSystem) return false;
    char filename[64];
    getCurrentFileName(l->name, filename, sizeof(filename));

    File file = fileSystem->open(filename, "w");
    if (!file) { Serial.printf("Error abriendo archivo: %s\n", filename); return false; }

    for (uint8_t i = 0; i < l->columnCount; i++) {
      file.print(l->columns[i].name);
      if (i < l->columnCount - 1) file.print(",");
    }
    file.println();
    file.close();
    Serial.printf("Header escrito en: %s\n", filename);
    return true;
  }

  bool writeEntry(LogFile* l) {
    if (!l || !fileSystem) return false;
    char filename[64];
    getCurrentFileName(l->name, filename, sizeof(filename));

    File file = fileSystem->open(filename, "a");
    if (!file) { Serial.printf("Error abriendo archivo para append: %s\n", filename); return false; }

    char buf[128];
    for (uint8_t i = 0; i < l->columnCount; i++) {
      formatValue(l->columns[i], buf, sizeof(buf));
      file.print(buf);
      if (i < l->columnCount - 1) file.print(",");
    }
    file.println();
    file.close();
    return true;
  }

  bool rotateFile(LogFile* l) {
    if (!l) return false;
    l->fileIndex++;
    l->currentEntries = 0;
    l->hasHeader = false;
    Serial.printf("Archivo rotado a índice: %d\n", l->fileIndex);
    if (rotationCallback) rotationCallback(l->name, l->fileIndex);
    return true;
  }

  bool checkAndFreeSpace() {
    if (getFreeSpace() < 10240) { Serial.printf("Espacio libre bajo: %lu bytes\n", getFreeSpace()); return false; }
    return true;
  }

  void formatValue(Column& col, char* buffer, size_t bufferSize) {
    switch (col.type) {
      case TYPE_INT:
        snprintf(buffer, bufferSize, "%d", col.varPtr ? *(int*)col.varPtr : 0);
        break;
      case TYPE_FLOAT:
        if (col.varPtr) dtostrf(*(float*)col.varPtr, 0, col.decimals, buffer);
        else            { buffer[0]='0'; buffer[1]='\0'; }
        break;
      case TYPE_BOOL:
        buffer[0] = (col.varPtr && *(bool*)col.varPtr) ? '1' : '0';
        buffer[1] = '\0';
        break;
      case TYPE_STRING:
        if (col.varPtr) { strncpy(buffer, ((String*)col.varPtr)->c_str(), bufferSize - 1); buffer[bufferSize-1]='\0'; }
        else            buffer[0] = '\0';
        break;
      case TYPE_TIMESTAMP: {
        double v;
        unsigned long ms = millis();
        switch (col.timeUnit) {
          case MS:      v = ms;             break;
          case SECONDS: v = ms / 1000.0;   break;
          case MINUTES: v = ms / 60000.0;  break;
          case HOURS:   v = ms / 3600000.0;break;
          case DAYS:    v = ms / 86400000.0;break;
          default:      v = ms;
        }
        dtostrf(v, 0, 3, buffer);
        break;
      }
      case TYPE_NTP_TIMESTAMP: {
        time_t now; time(&now);
        struct tm ti; localtime_r(&now, &ti);
        strftime(buffer, bufferSize, col.timeFormat, &ti);
        break;
      }
      default:
        buffer[0] = '\0';
    }
  }

  uint32_t getFileSize(const char* filename) {
    if (!fileSystem) return 0;
    File file = fileSystem->open(filename, "r");
    if (!file) return 0;
    uint32_t sz = file.size();
    file.close();
    return sz;
  }

  uint32_t countLinesInFile(const char* filename) {
    if (!fileSystem) return 0;
    File file = fileSystem->open(filename, "r");
    if (!file) return 0;
    uint32_t count = 0;
    char buf[MAX_READ_BUFFER];
    while (file.available()) {
      int len = file.readBytesUntil('\n', buf, sizeof(buf) - 1);
      if (len > 0) count++;
    }
    file.close();
    return count;
  }

private:
  // Helper para agregar columnas (evita duplicación)
  bool _addColumn(const char* logName, const char* colName, ColumnType type,
                  void* ptr, int decimals, TimeUnit unit, const char* fmt) {
    LogFile* l = findLog(logName);
    if (!l)                                   { Serial.println("Error: Log no encontrado");           return false; }
    if (l->columnCount >= MAX_COLUMNS)        { Serial.println("Error: Máximo de columnas alcanzado");return false; }
    if (strlen(colName) >= MAX_COLUMN_NAME)   { Serial.println("Error: Nombre de columna muy largo"); return false; }

    Column* col = &l->columns[l->columnCount];
    strncpy(col->name, colName, MAX_COLUMN_NAME - 1);
    col->name[MAX_COLUMN_NAME - 1] = '\0';
    col->type    = type;
    col->varPtr  = ptr;
    col->decimals= decimals;
    col->timeUnit= unit;
    strncpy(col->timeFormat, fmt, sizeof(col->timeFormat) - 1);
    col->timeFormat[sizeof(col->timeFormat) - 1] = '\0';
    l->columnCount++;
    return true;
  }
};

#endif // DATALOGGER_H