#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include "esp_camera.h"
#include "base64.h"

// ==========================================================
// CONFIGURAÇÕES
// ==========================================================

const char* WIFI_SSID = "NOME_DO_WIFI";
const char* WIFI_PASSWORD = "SENHA_DO_WIFI";

const char* ROBOFLOW_API_KEY = "SUA_API_KEY";

// Workflow do Roboflow
const char* WORKFLOW_URL =
  "https://serverless.roboflow.com/"
  "davi-costa-araujo/workflows/"
  "lixeira-inteligente-roteamento-1789919928507";

// Servo
constexpr int SERVO_PIN = 13;

// Posição de repouso
constexpr int SERVO_HOME_ANGLE = 180;

// Tempo para o lixo cair
constexpr unsigned long DIVERSION_TIME_MS = 2500;

// Intervalo entre capturas
constexpr unsigned long CAPTURE_INTERVAL_MS = 5000;

Servo routingServo;

unsigned long lastCapture = 0;


// ==========================================================
// PINOS DA ESP32-CAM AI THINKER
// ==========================================================

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22


// ==========================================================
// INICIALIZAÇÃO DA CÂMERA
// ==========================================================

bool initializeCamera() {

  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;

  config.pixel_format = PIXFORMAT_JPEG;

  // 320 x 240
  config.frame_size = FRAMESIZE_QVGA;

  config.jpeg_quality = 12;

  config.fb_count = 1;

  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
  }
  else {
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t result = esp_camera_init(&config);

  if (result != ESP_OK) {

    Serial.printf(
      "Erro ao inicializar camera: 0x%x\n",
      result
    );

    return false;
  }

  Serial.println("Camera inicializada!");

  return true;
}


// ==========================================================
// WIFI
// ==========================================================

void connectToWiFi() {

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print("Conectando ao Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  Serial.println("Wi-Fi conectado!");

  Serial.print("IP: ");

  Serial.println(
    WiFi.localIP()
  );
}


// ==========================================================
// PROCURA UMA CHAVE DENTRO DO JSON
// ==========================================================

JsonVariant findKey(
  JsonVariant value,
  const char* targetKey
) {

  if (value.is<JsonObject>()) {

    JsonObject object =
      value.as<JsonObject>();

    if (object.containsKey(targetKey)) {

      return object[targetKey];
    }

    for (JsonPair pair : object) {

      JsonVariant found =
        findKey(
          pair.value(),
          targetKey
        );

      if (!found.isNull()) {

        return found;
      }
    }
  }

  if (value.is<JsonArray>()) {

    for (JsonVariant item :
         value.as<JsonArray>()) {

      JsonVariant found =
        findKey(
          item,
          targetKey
        );

      if (!found.isNull()) {

        return found;
      }
    }
  }

  return JsonVariant();
}


// ==========================================================
// ROTEAMENTO DO LIXO
// ==========================================================

void routeWaste(
  const String& wasteClass
) {

  int routeCode = 0;

  int servoAngle = 180;

  String binColor = "desconhecido";


  // PAPEL / PAPELÃO
  if (wasteClass == "papel_papelao") {

    routeCode = 1;

    servoAngle = 0;

    binColor = "azul";
  }


  // PLÁSTICO
  else if (wasteClass == "plastico") {

    routeCode = 2;

    servoAngle = 36;

    binColor = "vermelho";
  }


  // VIDRO
  else if (wasteClass == "vidro") {

    routeCode = 3;

    servoAngle = 72;

    binColor = "verde";
  }


  // METAL
  else if (wasteClass == "metal") {

    routeCode = 4;

    servoAngle = 108;

    binColor = "amarelo";
  }


  // ORGÂNICO
  else if (wasteClass == "organico") {

    routeCode = 5;

    servoAngle = 144;

    binColor = "marrom";
  }


  // DESCONHECIDO
  else {

    Serial.println();
    Serial.println(
      "RESIDUO DESCONHECIDO!"
    );

    Serial.println(
      "Nenhuma rota sera acionada."
    );

    return;
  }


  // ========================================================
  // MOSTRA RESULTADO
  // ========================================================

  Serial.println();
  Serial.println(
    "========== CLASSIFICACAO =========="
  );

  Serial.print("Classe: ");

  Serial.println(
    wasteClass
  );

  Serial.print("Compartimento: ");

  Serial.println(
    binColor
  );

  Serial.print("Codigo: ");

  Serial.println(
    routeCode
  );

  Serial.print("Angulo: ");

  Serial.print(
    servoAngle
  );

  Serial.println(" graus");

  Serial.println(
    "==================================="
  );


  // ========================================================
  // MOVIMENTA SERVO
  // ========================================================

  routingServo.write(
    servoAngle
  );

  delay(700);


  // Dá tempo para o lixo cair

  delay(
    DIVERSION_TIME_MS
  );


  // Volta para posição inicial

  routingServo.write(
    SERVO_HOME_ANGLE
  );

  delay(700);
}


// ==========================================================
// CAPTURA E ENVIA IMAGEM AO ROBOFLOW
// ==========================================================

bool classifyAndRouteWaste() {

  // Verifica Wi-Fi

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println(
      "Wi-Fi desconectado."
    );

    connectToWiFi();
  }


  // ========================================================
  // CAPTURA IMAGEM
  // ========================================================

  camera_fb_t* frame =
    esp_camera_fb_get();

  if (frame == nullptr) {

    Serial.println(
      "Erro ao capturar imagem."
    );

    return false;
  }


  Serial.printf(
    "Imagem capturada: %u bytes\n",
    static_cast<unsigned int>(
      frame->len
    )
  );


  // ========================================================
  // CONVERTE PARA BASE64
  // ========================================================

  String encodedImage =
    base64::encode(
      frame->buf,
      frame->len
    );


  esp_camera_fb_return(frame);

  frame = nullptr;


  if (encodedImage.length() == 0) {

    Serial.println(
      "Erro ao converter imagem."
    );

    return false;
  }


  // ========================================================
  // CRIA REQUISIÇÃO
  // ========================================================

  String requestBody;

  requestBody.reserve(
    encodedImage.length() + 100
  );


  requestBody =
    "{\"inputs\":{\"image\":"
    "{\"type\":\"base64\","
    "\"value\":\"";


  requestBody +=
    encodedImage;


  requestBody +=
    "\"}}}";


  encodedImage = String();


  // ========================================================
  // HTTPS
  // ========================================================

  WiFiClientSecure secureClient;

  secureClient.setInsecure();

  secureClient.setTimeout(30);


  HTTPClient http;


  if (
    !http.begin(
      secureClient,
      WORKFLOW_URL
    )
  ) {

    Serial.println(
      "Erro ao iniciar HTTPS."
    );

    return false;
  }


  http.setTimeout(30000);


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  // ========================================================
  // API KEY
  // ========================================================

  String authorization =
    "Bearer ";

  authorization +=
    ROBOFLOW_API_KEY;


  http.addHeader(
    "Authorization",
    authorization
  );


  Serial.println(
    "Enviando imagem ao Roboflow..."
  );


  // ========================================================
  // ENVIA
  // ========================================================

  int statusCode =
    http.POST(
      reinterpret_cast<uint8_t*>(
        const_cast<char*>(
          requestBody.c_str()
        )
      ),
      requestBody.length()
    );


  requestBody = String();


  // ========================================================
  // VERIFICA RESPOSTA
  // ========================================================

  if (statusCode <= 0) {

    Serial.print(
      "Erro HTTP: "
    );

    Serial.println(
      http.errorToString(
        statusCode
      )
    );

    http.end();

    return false;
  }


  String response =
    http.getString();


  http.end();


  Serial.print(
    "Status HTTP: "
  );

  Serial.println(
    statusCode
  );


  if (
    statusCode < 200 ||
    statusCode >= 300
  ) {

    Serial.println(
      "Erro retornado pelo Roboflow:"
    );

    Serial.println(
      response
    );

    return false;
  }


  // ========================================================
  // INTERPRETA JSON
  // ========================================================

  DynamicJsonDocument document(
    32768
  );


  DeserializationError jsonError =
    deserializeJson(
      document,
      response
    );


  if (jsonError) {

    Serial.println(
      "Erro ao interpretar JSON."
    );

    Serial.println(
      response
    );

    return false;
  }


  JsonVariant root =
    document.as<JsonVariant>();


  // ========================================================
  // PEGA A CLASSE
  // ========================================================

  JsonVariant classValue =
    findKey(
      root,
      "waste_class"
    );


  if (
    classValue.isNull()
  ) {

    Serial.println(
      "O Roboflow nao retornou waste_class."
    );

    Serial.println(
      "Resposta:"
    );

    Serial.println(
      response
    );

    return false;
  }


  String wasteClass =
    classValue.as<String>();


  Serial.println();

  Serial.println(
    "IA identificou:"
  );

  Serial.println(
    wasteClass
  );


  // ========================================================
  // ROTEIA
  // ========================================================

  routeWaste(
    wasteClass
  );


  return true;
}


// ==========================================================
// SETUP
// ==========================================================

void setup() {

  Serial.begin(
    115200
  );

  delay(1500);


  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    "     LIXEIRA INTELIGENTE"
  );

  Serial.println(
    "================================"
  );


  // ========================================================
  // SERVO
  // ========================================================

  ESP32PWM::allocateTimer(1);

  ESP32PWM::allocateTimer(2);

  ESP32PWM::allocateTimer(3);


  routingServo.setPeriodHertz(
    50
  );


  routingServo.attach(
    SERVO_PIN,
    500,
    2400
  );


  routingServo.write(
    SERVO_HOME_ANGLE
  );


  // ========================================================
  // WIFI
  // ========================================================

  connectToWiFi();


  // ========================================================
  // CAMERA
  // ========================================================

  if (
    !initializeCamera()
  ) {

    Serial.println(
      "Falha na camera."
    );

    while (true) {

      delay(1000);
    }
  }


  Serial.println();

  Serial.println(
    "Sistema pronto!"
  );
}


// ==========================================================
// LOOP
// ==========================================================

void loop() {

  if (
    millis() - lastCapture >=
    CAPTURE_INTERVAL_MS
  ) {

    lastCapture =
      millis();


    bool success =
      classifyAndRouteWaste();


    if (!success) {

      Serial.println(
        "Falha na classificacao."
      );

      Serial.println(
        "Nenhuma rota foi acionada."
      );
    }
  }


  delay(50);
}
