#include <WiFi.h>
#include <EmonLib.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>


//===== Biblioteca do Diplay =====//
LiquidCrystal_I2C lcd(0x27, 16, 2);

//===== Dados de rede WI-fi =====//
const char* ssid = "Roberio";
const char* password = "18141981";

//===== Dados thingSpeak =====// 
String apiKey = "4LYP0ILQT1YBZ0FG";
String thingSpeakServer = "http://api.thingspeak.com/update";

//===== Dados do backend próprio (Node.js + MySQL na nuvem) =====//
// Depois do deploy (Railway/Render/etc.), troque pela URL pública real, ex:
// "https://seu-projeto.up.railway.app/api/leituras"
const char* backendServer = "https://SEU_BACKEND_NA_NUVEM/api/leituras";
const char* dispositivoId = "esp32_tcc";

unsigned long ultimoEnvioBackend = 0;
const unsigned long intervaloBackend = 60000; // envia consumo ao backend a cada 60s

WiFiServer server(80);

//===== DECLARAÇÕES DAS VARIAVEIS =====//
    //===== ZMPT101B - tensão=====
    const int pinZMPT_R = 36; // SP
    const int pinZMPT_S = 39; // SN
    const int pinZMPT_T = 33; // G33

    //===== SCT013 - corrente =====//
    const int pinSCT_R = 32; // G32
    const int pinSCT_S = 35; // G35
    const int pinSCT_T = 34; // G34

unsigned long ultimoEnvioThingSpeak = 0;
const unsigned long intervaloThingSpeak = 20000; // 20 segundos

//=====Variaveis medidas =====//
double voltageR = 0.0;
double voltageS = 0.0;
double voltageT = 0.0;
double currentR = 0.0;
double currentS = 0.0;
double currentT = 0.0;
double PowerR   = 0.0;
double PowerS   = 0.0;
double PowerT   = 0.0;
double apparentPower = 0.0;
double energy = 0.0;
double energyDeltaBackend = 0.0; // energia acumulada desde o último envio ao backend (zera após enviar)

unsigned long lastMeasure = 0;
const unsigned long interval = 2000;


float medirRMS(int pino,int amostras){
 float media = 0.0;
  float somaQuadrados = 0.0;
   
    for (int i = 0; i < amostras; i++){
      media += analogRead(pino);
     delayMicroseconds(200);
   }
    media /= amostras;

      for ( int i =0; i< amostras; i++){
        float amostra = analogRead(pino) - media;
         somaQuadrados += amostra * amostra;
        delayMicroseconds(200);
      }
      return sqrt(somaQuadrados / amostras);
    }
      
void setup() {
{
    lcd.init();
      lcd.backlight();

     lcd.setCursor(0,0);
  lcd.print("TCC ESP32");

  lcd.setCursor(0,1);
  lcd.print("Monitor Energia");
}

  Serial.begin(115200);
  delay(1000);

  Serial.println("Inicializando sistema...");

  analogReadResolution(12);

   WiFi.begin(ssid, password);
  Serial.println("Conectando ao Wi-Fi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi conectado!");
  Serial.print("IP do ESP32: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("Servidor iniciado");
}

void loop() {
  
  if (millis() - lastMeasure >= interval) {
    lastMeasure = millis();

    float rmsTensaoADC_R = medirRMS(pinZMPT_R, 500);
    float rmsTensaoADC_S = medirRMS(pinZMPT_S, 500);
    float rmsTensaoADC_T = medirRMS(pinZMPT_T, 500);
    float rmsCorrenteADC_R = medirRMS(pinSCT_R, 500);
    float rmsCorrenteADC_S = medirRMS(pinSCT_S, 500);
    float rmsCorrenteADC_T = medirRMS(pinSCT_T, 500);

//===== fatores iniciais de calibração tensão e corrente =====

       float fatorTensaoR = 0.505;
       float fatorTensaoS = 0.505;
       float fatorTensaoT = 0.505;

     if (rmsTensaoADC_R < 200 ){
         voltageR = 0.0;
     }
     else{ voltageR = rmsTensaoADC_R * fatorTensaoR;
     }
     if (rmsTensaoADC_S < 200 ){
         voltageS = 0.0;
     }
     else{ voltageS = rmsTensaoADC_S * fatorTensaoS;
     }
     if (rmsTensaoADC_T < 200 ){
         voltageT = 0.0;
     }
     else{ voltageT = rmsTensaoADC_T * fatorTensaoT;
     }     
   float fatorCorrenteR = 0.039;
    float fatorCorrenteS = 0.040;    
     float fatorCorrenteT = 0.039;             
     
     if (rmsCorrenteADC_R < 26 ) {
      currentR = 0.0;
     }
        else{ currentR = rmsCorrenteADC_R * fatorCorrenteR;        
     }
      if (rmsCorrenteADC_S < 26 ) {
      currentS = 0.0;
     }
        else{ currentS = rmsCorrenteADC_S * fatorCorrenteS;         
     }
      if (rmsCorrenteADC_T < 26 ) {
      currentT = 0.0;
     }
        else{ currentT = rmsCorrenteADC_T * fatorCorrenteT;         
     }

      //===== Potência aparente =====//
          PowerR = voltageR * currentR;
          PowerS = voltageS * currentS;
          PowerT = voltageT * currentT;
      apparentPower = PowerR + PowerS + PowerT;

      //===== Energia aproximada =====//
    energy += (apparentPower / 1000.0) * (interval / 3600000.0);
    energyDeltaBackend += (apparentPower / 1000.0) * (interval / 3600000.0);

//===== Mostrar no LCD =====//
lcd.setCursor(0, 0);
lcd.print("V:");
lcd.print(voltageR, 0);
lcd.print(" I:");
lcd.print(currentR, 1);

lcd.setCursor(0, 1);
lcd.print("P:");
lcd.print(apparentPower, 0);
lcd.print(" E:");
lcd.print(energy, 5);

//===== ENVIO PARA DE DADOS PARA THINGSPEAK =====//
if (millis() - ultimoEnvioThingSpeak >= intervaloThingSpeak) {
  ultimoEnvioThingSpeak = millis();
if (WiFi.status() == WL_CONNECTED){
  HTTPClient http;  
  String url = String("http://api.thingspeak.com/update?api_key=4LYP0ILQT1YBZ0FG") +
               "&field1=" + String(currentR, 2) +
               "&field2=" + String(voltageR, 2) +
               "&field3=" + String(voltageS, 2) +
               "&field4=" + String(currentS, 2) +
               "&field5=" + String(voltageT, 2) +
               "&field6=" + String(currentT, 2) +
               "&field7=" + String(apparentPower, 2) +
               "&field8=" + String(energy, 4);             
http.begin(url);
  int httpResponseCode = http.GET();  
if (httpResponseCode == 200) {
   Serial.println("Dados enviados para o servidor com sucesso.");   
}
else {
    Serial.print("Falha na comunicacao. Codigo: ");
    Serial.println(httpResponseCode);
}
   http.end();
 }  
}

//===== ENVIO PARA O BACKEND PRÓPRIO NA NUVEM (Node.js + MySQL) =====//
if (millis() - ultimoEnvioBackend >= intervaloBackend) {
  ultimoEnvioBackend = millis();
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient httpBackend;
    WiFiClientSecure clienteSeguro;
    clienteSeguro.setInsecure(); // não valida certificado - ok para TCC/protótipo
    httpBackend.begin(clienteSeguro, backendServer);
    httpBackend.addHeader("Content-Type", "application/json");

    String payload = String("{") +
      "\"dispositivo_id\":\"" + dispositivoId + "\"," +
      "\"tensao_r\":" + String(voltageR, 2) + "," +
      "\"tensao_s\":" + String(voltageS, 2) + "," +
      "\"tensao_t\":" + String(voltageT, 2) + "," +
      "\"corrente_r\":" + String(currentR, 3) + "," +
      "\"corrente_s\":" + String(currentS, 3) + "," +
      "\"corrente_t\":" + String(currentT, 3) + "," +
      "\"potencia_aparente\":" + String(apparentPower, 2) + "," +
      "\"energia_kwh\":" + String(energyDeltaBackend, 6) +
      "}";

    int codigoBackend = httpBackend.POST(payload);

    if (codigoBackend == 200 || codigoBackend == 201) {
      Serial.println("Dados enviados ao backend com sucesso.");
      energyDeltaBackend = 0.0; // só zera o delta se o envio deu certo
    } else {
      Serial.print("Falha ao enviar ao backend. Codigo: ");
      Serial.println(codigoBackend);
      // mantém energyDeltaBackend acumulado para tentar de novo no próximo ciclo
    }
    httpBackend.end();
  }
}
     //===== DADOS PARA INFORMAÇÃO NO SERIAL MONITOR =====//
//===== DA PLATAFORMA DE PROGRAMAÇÃO DO ESP32 ARDUINO IDE =====//
 Serial.println("===== LEITURA_R =====");
     Serial.print("RMS ADC Tensao: ");
        Serial.println(rmsTensaoADC_R);           
           Serial.print("RMS ADC corrente: ");
             Serial.println(rmsCorrenteADC_R);           
           Serial.print("Tensao estimada: ");
         Serial.print(voltageR, 2);
        Serial.println(" V ");    
     Serial.print("Corrente: ");
  Serial.print(currentR, 3);
Serial.println(" A");

 Serial.println("===== LEITURA_S =====");
    Serial.print("RMS ADC Tensao: ");
       Serial.println(rmsTensaoADC_S);           
          Serial.print("RMS ADC corrente: ");
             Serial.println(rmsCorrenteADC_S);           
           Serial.print("Tensao estimada: ");
         Serial.print(voltageS, 2);
       Serial.println(" V ");    
     Serial.print("Corrente: ");
   Serial.print(currentS, 3);
 Serial.println(" A");
        
 Serial.println("===== LEITURA_T =====");
     Serial.print("RMS ADC Tensao: ");
        Serial.println(rmsTensaoADC_T);           
           Serial.print("RMS ADC corrente: ");
             Serial.println(rmsCorrenteADC_T);           
           Serial.print("Tensao estimada: ");
         Serial.print(voltageT, 2);
        Serial.println(" V ");    
     Serial.print("Corrente: ");
  Serial.print(currentT, 3);
Serial.println(" A");

 Serial.println("===== Potência Total =====");
     Serial.print("Potencia aparente: ");
        Serial.print(apparentPower, 2);
           Serial.println(" VA");

            Serial.print("Energia: ");
         Serial.print(energy, 6);
      Serial.println(" kWh");
 Serial.println("===================");
  }

  WiFiClient client = server.available();
  if (client) {
    while (client.connected() && !client.available()) {
      delay(1);
    }

    if (client.available()) {
      client.readStringUntil('\r');
      client.readStringUntil('\n');
    }

    client.println("HTTP/1.1 200 OK");
       client.println("Content-Type: text/html; charset=UTF-8");
         client.println("Connection: close");
          client.println();

           client.println("<!DOCTYPE html><html><head>");
         client.println("<meta charset='UTF-8'>");
       client.println("<meta http-equiv='refresh' content='3'>");
     client.println("<title>Monitoramento</title>");
  client.println("</head><body>");

  client.println("<h1>Sistema de Monitoramento</h1>");
     client.println("<h1>Consumo de Energia</h1>");
        client.println("<p>TensaoR: " + String(voltageR, 2) + " V</p>");
         client.println("<p>CorrenteR: " + String(currentR, 3) + " A</p>");
          client.println("<p>TensaoS: " + String(voltageS, 2) + " V</p>");
           client.println("<p>CorrenteS: " + String(currentS, 3) + " A</p>");
           client.println("<p>TensaoT: " + String(voltageT, 2) + " V</p>");
         client.println("<p>CorrenteT: " + String(currentT, 3) + " A</p>");         
      client.println("<p>Potencia Aparente: " + String(apparentPower, 2) + " VA</p>");
   client.println("<p>Energia: " + String(energy, 6) + " kWh</p>");

    client.println("</body></html>");
    client.stop();
  }
}
