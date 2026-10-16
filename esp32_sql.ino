#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <sqlite3.h>
#include <ArduinoJson.h>
#include <uri/UriBraces.h> //permite el uso de parámetros dinámicos en las rutas de las URLs utilizando llaves vacías {} como comodin

// =====================================================
// CONFIGURACION WIFI
// =====================================================

const char* WIFI_SSID = "xxxxxx";
const char* WIFI_PASSWORD = "xxxxxx";


// =====================================================
// SERVIDOR HTTP
// =====================================================

WebServer server(80);


// =====================================================
// SQLITE
// =====================================================

sqlite3* db = nullptr;

const char* DB_PATH = "/spiffs/iot.db";


// =====================================================
// EJECUTA SQLITE
// =====================================================

bool executeSQL(const char* sql) {

  char* errorMessage = nullptr;

  int rc = sqlite3_exec(
    db,
    sql,
    nullptr,
    nullptr,
    &errorMessage);

  if (rc != SQLITE_OK) {
    Serial.print("SQLite error: ");

    if (errorMessage) {
      Serial.println(errorMessage);
      sqlite3_free(errorMessage);
    }
    return false;
  }
  return true;
}


// =====================================================
// INICIALIZAR BASE
// =====================================================

bool initDatabase() {

  Serial.println();
  Serial.println("Inicializando SQLite...");

  sqlite3_initialize();

  int rc = sqlite3_open(DB_PATH, &db);

  if (rc != SQLITE_OK) {

    Serial.print("No se pudo abrir SQLite: ");
    Serial.println(sqlite3_errmsg(db));

    return false;
  }

  Serial.println("Base SQLite OK");

  // Con "R" todo el bloque entre los paréntesis se almacena exactamente como está escrito, sin necesidad de agregar \n al final de cada línea ni dividir la cadena en múltiples fragmentos
  const char* createTable = R"sql(

    CREATE TABLE IF NOT EXISTS data (
      id INTEGER PRIMARY KEY,
      device TEXT NOT NULL,
      sensor TEXT NOT NULL,
      value REAL NOT NULL,
      timestamp INTEGER NOT NULL
    );

  )sql";

  if (!executeSQL(createTable)) {
    return false;
  }

  Serial.println("Tabla data disponible");
  return true;
}


// =====================================================
// GET /api/status
// =====================================================

void handleStatus() {

  JsonDocument json;

  json["estado"] = "ok";
  json["equipo"] = "ESP32 SQLite API REST Server";

  json["ip"] = WiFi.localIP().toString();

  json["memoria_libre"] = ESP.getFreeHeap();

  json["spiffs_total"] = SPIFFS.totalBytes();
  json["spiffs_usado"] = SPIFFS.usedBytes();

  String response;

  serializeJson(json, response);

  server.send(
    200,
    "application/json",
    response);
}


// =====================================================
// POST /api/data
//
// JSON esperado:
// {
//   "device":"esp32-01",
//   "sensor":"temperature",
//   "value":24.5,
//   "timestamp":1770000000
// }
// =====================================================

void handlePostData() {

  if (!server.hasArg("plain")) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"body requerido\"}");

    return;
  }

  String body = server.arg("plain");

  JsonDocument json;

  DeserializationError error =
    deserializeJson(json, body);

    Serial.println("==== JSON Interpretado ====");
    serializeJsonPretty(json, Serial);
    Serial.println();
    Serial.println("=");

  if (error) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"JSON invalido\"}");

    return;
  }


  const char* device =
    json["device"].as<const char*>();

  const char* sensor =
    json["sensor"].as<const char*>();


  if (
    device == nullptr || sensor == nullptr || json["value"].isNull() || json["timestamp"].isNull()) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"campos incompletos\"}");

    return;
  }


  double value =
    json["value"].as<double>();

  int64_t timestamp =
    json["timestamp"].as<int64_t>();


  const char* sql =
    "INSERT INTO data "
    "(device, sensor, value, timestamp) "
    "VALUES (?, ?, ?, ?);";


  sqlite3_stmt* stmt = nullptr;


  int rc = sqlite3_prepare_v2(
    db,
    sql,
    -1,
    &stmt,
    nullptr);


  if (rc != SQLITE_OK) {

    server.send(
      500,
      "application/json",
      "{\"error\":\"prepare SQL\"}");

    return;
  }


  sqlite3_bind_text(
    stmt,
    1,
    device,
    -1,
    SQLITE_TRANSIENT);

  sqlite3_bind_text(
    stmt,
    2,
    sensor,
    -1,
    SQLITE_TRANSIENT);

  sqlite3_bind_double(
    stmt,
    3,
    value);

  sqlite3_bind_int64(
    stmt,
    4,
    timestamp);


  rc = sqlite3_step(stmt);


  if (rc != SQLITE_DONE) {

    Serial.println(
      sqlite3_errmsg(db));

    sqlite3_finalize(stmt);

    server.send(
      500,
      "application/json",
      "{\"error\":\"insert SQL\"}");

    return;
  }


  sqlite3_finalize(stmt);


  long id =
    sqlite3_last_insert_rowid(db);


  JsonDocument responseJson;

  responseJson["status"] = "created";
  responseJson["id"] = id;


  String response;

  serializeJson(
    responseJson,
    response);


  server.send(
    201,
    "application/json",
    response);
}


// =====================================================
// GET /api/data
// =====================================================

void handleGetData() {

  int limit = 50;

  if (server.hasArg("limit")) {

    limit = server.arg("limit").toInt();

    if (limit <= 0)
      limit = 50;

    if (limit > 100)
      limit = 100;
  }


  const char* sql =

    "SELECT "
    "id, device, sensor, value, timestamp "
    "FROM data "
    "ORDER BY id DESC "
    "LIMIT ?;";


  sqlite3_stmt* stmt = nullptr;


  int rc = sqlite3_prepare_v2(
    db,
    sql,
    -1,
    &stmt,
    nullptr);


  if (rc != SQLITE_OK) {

    server.send(
      500,
      "application/json",
      "{\"error\":\"consulta SQL\"}");

    return;
  }


  sqlite3_bind_int(
    stmt,
    1,
    limit);


  JsonDocument json;

  JsonArray array =
    json["data"].to<JsonArray>();


  while (
    sqlite3_step(stmt)
    == SQLITE_ROW) {

    JsonObject row =
      array.add<JsonObject>();


    row["id"] =
      sqlite3_column_int(
        stmt,
        0);


    row["device"] =
      (const char*)
        sqlite3_column_text(
          stmt,
          1);


    row["sensor"] =
      (const char*)
        sqlite3_column_text(
          stmt,
          2);


    row["value"] =
      sqlite3_column_double(
        stmt,
        3);


    row["timestamp"] =
      sqlite3_column_int64(
        stmt,
        4);
  }


  sqlite3_finalize(stmt);


  String response;

  serializeJson(
    json,
    response);


  server.send(
    200,
    "application/json",
    response);
}


// =====================================================
// GET /api/data/{id}
// =====================================================

void handleGetDataById() {

  int id =
    server.pathArg(0).toInt();


  const char* sql =

    "SELECT "
    "id, device, sensor, value, timestamp "
    "FROM data "
    "WHERE id = ?;";


  sqlite3_stmt* stmt = nullptr;


  sqlite3_prepare_v2(
    db,
    sql,
    -1,
    &stmt,
    nullptr);


  sqlite3_bind_int(
    stmt,
    1,
    id);


  if (
    sqlite3_step(stmt)
    != SQLITE_ROW) {

    sqlite3_finalize(stmt);

    server.send(
      404,
      "application/json",
      "{\"error\":\"registro no encontrado\"}");

    return;
  }


  JsonDocument json;


  json["id"] =
    sqlite3_column_int(
      stmt,
      0);


  json["device"] =
    (const char*)
      sqlite3_column_text(
        stmt,
        1);


  json["sensor"] =
    (const char*)
      sqlite3_column_text(
        stmt,
        2);


  json["value"] =
    sqlite3_column_double(
      stmt,
      3);


  json["timestamp"] =
    sqlite3_column_int64(
      stmt,
      4);


  sqlite3_finalize(stmt);


  String response;

  serializeJson(
    json,
    response);


  server.send(
    200,
    "application/json",
    response);
}


// =====================================================
// DELETE /api/data/{id}
// =====================================================

void handleDeleteData() {

  int id =
    server.pathArg(0).toInt();


  const char* sql =

    "DELETE FROM data "
    "WHERE id = ?;";


  sqlite3_stmt* stmt = nullptr;


  sqlite3_prepare_v2(
    db,
    sql,
    -1,
    &stmt,
    nullptr);


  sqlite3_bind_int(
    stmt,
    1,
    id);


  int rc =
    sqlite3_step(stmt);


  if (rc != SQLITE_DONE) {

    sqlite3_finalize(stmt);

    server.send(
      500,
      "application/json",
      "{\"error\":\"delete SQL\"}");

    return;
  }


  int affected =
    sqlite3_changes(db);


  sqlite3_finalize(stmt);


  if (affected == 0) {

    server.send(
      404,
      "application/json",
      "{\"error\":\"registro no encontrado\"}");

    return;
  }


  server.send(
    200,
    "application/json",
    "{\"status\":\"deleted\"}");
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);


  Serial.println();
  Serial.println(
    "ESP32-S3 SQLite REST Server");


  // ---------------------------------------------
  // SPIFFS
  // ---------------------------------------------

  if (!SPIFFS.begin(true)) {

    Serial.println(
      "Error montando SPIFFS");

    return;
  }


  Serial.println(
    "SPIFFS montado");


  Serial.printf(
    "Total: %u bytes\n",
    SPIFFS.totalBytes());


  Serial.printf(
    "Usado: %u bytes\n",
    SPIFFS.usedBytes());


  // ---------------------------------------------
  // SQLITE
  // ---------------------------------------------

  if (!initDatabase()) {

    Serial.println(
      "Error inicializando SQLite");

    return;
  }


  // ---------------------------------------------
  // WIFI
  // ---------------------------------------------

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD);


  Serial.print(
    "Conectando WiFi");


  while (
    WiFi.status()
    != WL_CONNECTED) {

    delay(500);

    Serial.print(".");
  }


  Serial.println();
  Serial.println(
    "WiFi conectado");


  Serial.print(
    "IP: ");

  Serial.println(
    WiFi.localIP());


  // ---------------------------------------------
  // API REST
  // ---------------------------------------------

  server.on(
    "/api/status",
    HTTP_GET,
    handleStatus);


  server.on(
    "/api/data",
    HTTP_POST,
    handlePostData);


  server.on(
    "/api/data",
    HTTP_GET,
    handleGetData);


  server.on(
    UriBraces("/esp32/data/{}"),
    HTTP_GET,
    handleGetDataById);


  server.on(
    UriBraces("/api/data/{}"),
    HTTP_DELETE,
    handleDeleteData);


  server.enableCORS(true);


  server.onNotFound(
    []() {
      server.send(
        404,
        "application/json",
        "{\"error\":\"endpoint no encontrado\"}");
    });


  server.begin();


  Serial.println();
  Serial.println(
    "REST API iniciada.");
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  server.handleClient();

  delay(2);
}