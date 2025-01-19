#include<WiFi.h>
#include <PubSubClient.h>
#include <FS.h>
#include "SPIFFS.h"
#include <NTPClient.h> // https://github.com/taranais/NTPClient
// https://www.efeitonerd.com.br/2021/04/data-e-hora-no-esp32-ntp.html ↑
// #include "DHTesp.h" // simulação wokwi

/* Módulo Jardim inteligente
Conectar ao wifi para realizar a comunição com o AdafruitIO (acender led builtin quando conectado)
Fazer leitura de umidade do solo
Fazer o tratamento dos dados
Armazenar informações pertinentes ao módulo em arquivo de log
Gerar um dashboard no AdafruitIO para gerenciamento das informações e alertas
Led RGB para indicar o nível de umidade
 - Verde: Regada recentemente
 - Amarelo: Regada há um tempo
 - Vermelho: Precisando regar

LÓGICA:
Se umidade <= LIMITE_MINIMO: Notificação para aguar imediatamente e LED Vermelho.
Se umidade < LIMITE_MAXIMO - ((LIMITE_MAXIMO - LIMITE_MINIMO)/2): Notificação e LED Amarelo.
Se umidade >= LIMITE_MAXIMO: Limpar área de notificações e LED Verde.
*/


#define IO_USERNAME "CHANGE_HERE"
#define IO_KEY "CHANGE_HERE"

#define PIN_RGB_B 32
#define PIN_RGB_G 33
#define PIN_RGB_R 25
#define PIN_SENSOR 26

const char* ssid = "CHANGE_HERE";
const char* password ="CHANGE_HERE";

const char* mqttserver = "io.adafruit.com";
const int mqttport = 1883;
const char* mqttUser = IO_USERNAME;
const char* mqttPassword = IO_KEY;

const int BOARD_RESOLUTION = 4096; // The analogic board resolution, for example Arduino Uno is 10 bit (from 0 to 1023)
const float OPERATIONAL_VOLTAGE = 3.3; // The default ESP32 voltage
const float MAX_SENSOR_VOLTAGE = 3.0; // The maximum voltage that the sensor can output
const float SENSOR_READ_RATIO = OPERATIONAL_VOLTAGE / MAX_SENSOR_VOLTAGE; // The ratio betwent the two voltages

const int DRY = 520;
const int WET = 430;

WiFiUDP ntpUDP;
NTPClient ntp(ntpUDP);

WiFiClient espClient;
PubSubClient client(espClient);

// valores padrões para impedir que uma planta morra
double MAX_HUMIDITY = 75.00;
double MIN_HUMIDITY = 30.00;

double lastHumidityValue = 0.00;

// DHTesp dhtSensor; // simulação wokwi

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando conexão MQTT...");
    String clientId = "ESP32 - Sensores"; // Create a random client ID
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
      // Serial.println("conectado");
      client.subscribe("breno_almeida/feeds/min-humidity");
      client.subscribe("breno_almeida/feeds/max-humidity");
    } else {
      Serial.print("Falha, rc=");
      Serial.print(client.state());
      Serial.println(" Tentando novamente em 5s");
      delay(5000);
    }
  }
}

void writeFile(String line, String path, const char* mode="a", bool ln=true) {
  File rFile = SPIFFS.open(path, mode);
  if (!rFile) {
    Serial.println("Erro ao abrir arquivo!");
  }
  else {
    // Serial.print("tamanho");
    // Serial.println(rFile.size());
    if (ln) rFile.println(line);
    else rFile.print(line);
    // Serial.print("Gravou: ");
    // Serial.println(line);
  }
  rFile.close();
}

String readFile(String path) {
  String s;
  Serial.println("Read file");
  File rFile = SPIFFS.open(path, "r"); // r+ leitura e escrita
  if (!rFile) {
    Serial.println("Erro ao abrir arquivo!");
  }
  else {
    Serial.print("----------Lendo arquivo ");
    Serial.print(path);
    Serial.println("  ---------");
    while (rFile.position() < rFile.size())
    {
      s = rFile.readStringUntil('\n');
      s.trim();
      Serial.println(s);
    }
    rFile.close();
    return s;
  }
}

void formatFile() {
  Serial.println("Formantando SPIFFS");
  SPIFFS.format();
  Serial.println("Formatou SPIFFS");
}

void openFS(void) {
  if (!SPIFFS.begin()) {
    Serial.println("\nErro ao abrir o sistema de arquivos");
  }
  else {
    Serial.println("\nSistema de arquivos aberto com sucesso!");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Messagem recebida [");
  Serial.print(topic);
  Serial.print("] ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    messageTemp += (char)payload[i];
  }
  Serial.println();

  if (topic == "min_humidity") {
    MIN_HUMIDITY = messageTemp.toDouble();
    writeFile(String(MIN_HUMIDITY, 1), "/files/min_humidity.txt", "w", false); // spiffs
  } else if (topic == "max_humidity") {
    MAX_HUMIDITY = messageTemp.toDouble();
    writeFile(String(MAX_HUMIDITY, 1), "/files/max_humidity.txt", "w", false); // spiffs
  }
}

void turnLedOff() {
  digitalWrite(PIN_RGB_B, LOW);
  digitalWrite(PIN_RGB_G, LOW);
  digitalWrite(PIN_RGB_R, LOW);
}

void turnLedRed() {
  digitalWrite(PIN_RGB_R, HIGH);
}

void turnLedYellow() {
  digitalWrite(PIN_RGB_R, HIGH);
  digitalWrite(PIN_RGB_G, HIGH);
}

void turnLedGreen() {
  digitalWrite(PIN_RGB_G, HIGH);
}

void setup() {
  Serial.begin(9600);

  delay(5000); // delay para esperar o serial aparecer
  
  openFS(); // spiffs
  String str; // spiffs

  readFile("/files/humidity_log.txt"); // spiffs

  pinMode(LED_BUILTIN, OUTPUT); // Para sinalizar que o wi-fi foi conectado com sucesso
  pinMode(PIN_RGB_B, OUTPUT);
  digitalWrite(PIN_RGB_B, HIGH); // Sinalizar que o módulo foi ligado
  delay(1000);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_SENSOR, INPUT);

  // Ler valores de umidade mínima e máxima da planta do adafruit e guardar nas variáveis
  str = readFile("/files/min_humidity.txt"); // spiffs
  if (str != nullptr) MIN_HUMIDITY = str.toDouble(); // spiffs
  
  str = readFile("/files/max_humidity.txt"); // spiffs
  if (str != nullptr) MAX_HUMIDITY = str.toDouble(); // spiffs

  // dhtSensor.setup(PIN_SENSOR, DHTesp::DHT22); // simulação wokwi

  Serial.print("Connecting to ");
  Serial.print(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  digitalWrite(LED_BUILTIN, HIGH); // Para sinalizar que o wi-fi foi conectado com sucesso

  ntp.begin();
  ntp.setTimeOffset(-10800); // fuso horário -3h

  client.setServer(mqttserver, mqttport); // Publicar
  client.setCallback(callback); // Receber mensagem
}

void loop() {
  String notificationMsg = "";
  String dashboardColor = "";
  double humidity;

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // TempAndHumidity data = dhtSensor.getTempAndHumidity(); // simulação wokwi
  // humidity = data.humidity // simulação wokwi
  // Serial.println("Humidity: " + String(humidity)); // valor em porcentagem: [0.00, 100.00] // simulação wokwi
  int moistureAnalogicVal = analogRead(PIN_SENSOR) * SENSOR_READ_RATIO; // Read the analogic data and convert it to [0, 4095] range
  Serial.println(moistureAnalogicVal);
  humidity = map(moistureAnalogicVal, 3400, 1400, 0, 100); // para o sensor usado
  
  if(humidity > lastHumidityValue + 2) { // umidade atual > umidade lida anteriormente => planta aguada; +2% de margem de erro
    // escrever no arquivo .log se a umidade for diferente de 0
    writeFile(ntp.getFormattedDate() + ": ", "/files/humidity_log.txt", "a", false);
    writeFile("Umidade " + String(humidity, 1) + "%.", "/files/humidity_log.txt"); // spiffs
  }

  if(humidity <= MIN_HUMIDITY) {
    turnLedOff();
    turnLedRed();
    // Notificação para aguar imediatamente e cor vermelha
    notificationMsg = "Solo está seco, necessitando de água de imediato!";
    dashboardColor = "#FF0000";
  } else if (humidity < (MAX_HUMIDITY - ((MAX_HUMIDITY - MIN_HUMIDITY)/2))) {
    turnLedOff();
    turnLedYellow();
    // Notificação que é bom aguar e cor amarela
    notificationMsg = "Solo está ficando seco.";
    dashboardColor = "#FFFF00";
  } else {
    turnLedOff();
    turnLedGreen();
    // Limpar área de notificações no adafruit e cor verde
    notificationMsg = "-";
    dashboardColor = "#00FF00";
  }
  client.publish("breno_almeida/feeds/rgb-led", dashboardColor.c_str());
  client.publish("breno_almeida/feeds/module-humidity", String(humidity, 1).c_str());
  client.publish("breno_almeida/feeds/notification-msg", notificationMsg.c_str());
  // Serial.println(dashboardColor);
  // Serial.println(String(humidity, 1));
  // Serial.println(notificationMsg);

  lastHumidityValue = humidity;

  delay(300000); // Intervalo de 5min entre leituras
}