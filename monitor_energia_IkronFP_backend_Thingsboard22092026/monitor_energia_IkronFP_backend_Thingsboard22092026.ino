#include <WiFi.h>
#include <EmonLib.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

  
//=======Dados comunicação com IKron=========//
IPAddress usrIP(192, 168, 29, 68);
const uint16_t usrPort = 502;
WiFiClient usrClient;

  
//=====  Dados e Biblioteca do Diplay =====//
LiquidCrystal_I2C lcd(0x27, 16, 2);

    unsigned long ultimoTempoLCD = 0;
    const unsigned long intervaloLCD = 5000;
    bool telaTensao = true;

//===== Dados de rede WI-fi =====//
    unsigned long ultimaTentativaWiFi = 0;
    const unsigned long intervaloReconexaoWiFi = 10000;
    const char* ssid = "Roberio";
    const char* password = "18141981";
    bool wifiEstavaConectado = false;

//===== Dados do backend próprio (Node.js + MySQL na nuvem) =====//
// Depois do deploy (Railway/Render/etc.), troque pela URL pública real, ex:
// "https://seu-projeto.up.railway.app/api/leituras"

const char* backendServer = "https://monitor-energia-backend-mxns.onrender.com/api/leituras";
const char* dispositivoId = "esp32_tcc";

unsigned long ultimoEnvioBackend = 0;
const unsigned long intervaloBackend = 60000; // envia consumo ao backend a cada 60s

//===== Dados do ThingsBoard Cloud =====//
const char* thingsboardServer = "http://thingsboard.cloud/api/v1/5rUxhN7zLU73wb5uAW6U/telemetry";

unsigned long ultimoEnvioThingsboard = 0;
const unsigned long intervaloThingsboard = 30000; // envia a cada 30s (pode ajustar)

//===== DECLARAÇÕES DAS VARIAVEIS =====//
    //===== ZMPT101B - tensão=====
    const int pinZMPT_R = 36; // SP
    const int pinZMPT_S = 39; // SN
    const int pinZMPT_T = 33; // G33

    //===== SCT013 - corrente =====//
    const int pinSCT_R = 32; // G32
    const int pinSCT_S = 35; // G35
    const int pinSCT_T = 34; // G34

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
    double energyDeltaBackend = 0.0; 
    double activePower = 0.0;
    double energydelta = 0.0;

//=====Temporizador ikron======///
unsigned long ultimaLeituraIkron = 0;
const unsigned long intervaloIkron = 5000; 

unsigned long lastMeasure = 0;
const unsigned long interval = 2000;

unsigned long tempoEnergiaAnterior = 0;

 //=======Variaveis ikron======//
  float tensaoIkronL1 = 0.0;
  float tensaoIkronL2 = 0.0;
  float tensaoIkronL3 = 0.0;
  float fpIkronL1 = 0.0;
  float fpIkronL2 = 0.0;
  float fpIkronL3 = 0.0;

//=========teste de amostragem MAX e MIN========//
float medirRMS(int pino, int amostras) {

  double soma = 0.0;
  double somaQuadrados = 0.0;

  int minimo = 4095;
  int maximo = 0;

  for (int i = 0; i < amostras; i++) {

    int leitura = analogRead(pino);

    if (leitura < minimo) minimo = leitura;
    if (leitura > maximo) maximo = leitura;

    soma += leitura;
    somaQuadrados += (double)leitura * leitura;

    delayMicroseconds(200);
  }
  // Média do sinal (offset)
  double media = soma / amostras;

  // Média dos quadrados
  double mediaQuadrados = somaQuadrados / amostras;

  // RMS somente da componente alternada
  double rms = sqrt(mediaQuadrados - (media * media));

  //======Diagnóstico=========//

  Serial.print("ADC pino ");
  Serial.print(pino);

  Serial.print(" | Media: ");
  Serial.print(media);

  Serial.print(" | Min: ");
  Serial.print(minimo);

  Serial.print(" | Max: ");
  Serial.print(maximo);

  Serial.print(" | RMS: ");
  Serial.println(rms);
    
  return (float)rms;
 
}
//===Fim teste==//
/*float medirRMS(int pino, int amostras) {

  double soma = 0.0;
  double somaQuadrados = 0.0;  

  //==========Amostragem das medições RMS,única aquisição=========//
  for (int i = 0; i < amostras; i++) {

    int leitura = analogRead(pino);

    soma += leitura;
    somaQuadrados += (double)leitura * leitura;

    delayMicroseconds(200);
  }

  // Valor médio (offset)
  double media = soma / amostras;

  // RMS somente da componente
  double mediaQuadrados = somaQuadrados / amostras;

  double rms = sqrt(mediaQuadrados - (media * media));

  return (float)rms;
}*/

//=======DADOS DO IKRON========//

bool lerIkronTeste() {

  if (!usrClient.connect(usrIP, usrPort)) {
    Serial.println("Falha ao conectar no USR para leitura Modbus.");
    return false;
  }

  // Modbus TCP:
  // Unit ID = 1
  // Function = 04
  // Start Address = 10
  // Quantity = 2 registradores

  uint8_t requisicao[] = {
    0x00, 0x01,       // Transaction ID
    0x00, 0x00,       // Protocol ID
    0x00, 0x06,       // Length
    0x01,             // Unit ID = 1
    0x04,             // Function 04 - Input Registers
    0x00, 0x0A,       // Start Address = 10
    0x00, 0x06        // 6 registradores
  };

  usrClient.write(requisicao, sizeof(requisicao));

  unsigned long inicio = millis();

  while (usrClient.available() < 21) {
    if (millis() - inicio > 2000) {
      Serial.println("Timeout lendo iKron.");
      usrClient.stop();
      return false;
    }
  }
  uint8_t resposta[21];

  for (int i = 0; i < 21; i++) {
    resposta[i] = usrClient.read();
  }

  Serial.println("Resposta iKron:");

  for (int i = 0; i < 21; i++) {
    if (resposta[i] < 0x10) Serial.print("0");
    Serial.print(resposta[i], HEX);
    Serial.print(" ");
  }

  Serial.println();

// ===== Converte L1 =====
uint8_t bytesL1[4] = {
  resposta[9],
  resposta[10],
  resposta[11],
  resposta[12]
};
memcpy(&tensaoIkronL1, bytesL1, sizeof(float));

// ===== Converte L2 =====
uint8_t bytesL2[4] = {
  resposta[13],
  resposta[14],
  resposta[15],
  resposta[16]
};
memcpy(&tensaoIkronL2, bytesL2, sizeof(float));
  
// ===== Converte L3 =====
uint8_t bytesL3[4] = {
  resposta[17],
  resposta[18],
  resposta[19],
  resposta[20]
};
memcpy(&tensaoIkronL3, bytesL3, sizeof(float));


Serial.println("===== TENSOES iKRON =====");

Serial.print("L1 iKron: ");
Serial.print(tensaoIkronL1, 2);
Serial.println(" V");

Serial.print("L2 iKron: ");
Serial.print(tensaoIkronL2, 2);
Serial.println(" V");

Serial.print("L3 iKron: ");
Serial.print(tensaoIkronL3, 2);
Serial.println(" V");

usrClient.stop();

  return true;
}

bool lerFatorPotenciaIkron() {

  if (!usrClient.connect(usrIP, usrPort)) {
    Serial.println("Falha ao conectar no USR para ler FP.");
    return false;
  }

  // FP1 = 30061
  // FP2 = 30063
  // FP3 = 30065
  // Endereço Modbus inicial = 60
  // Total = 6 registradores

  uint8_t requisicaoFP[] = {
    0x00, 0x02,       // Transaction ID
    0x00, 0x00,       // Protocol ID
    0x00, 0x06,       // Length
    0x01,             // Unit ID = 1
    0x04,             // Function 04
    0x00, 0x3C,       // Start Address = 60
    0x00, 0x06        // 6 registradores
  };

  usrClient.write(requisicaoFP, sizeof(requisicaoFP));

  unsigned long inicio = millis();

  while (usrClient.available() < 21) {
    if (millis() - inicio > 2000) {
      Serial.println("Timeout lendo FP do iKron.");
      usrClient.stop();
      return false;
    }
  }

  uint8_t resposta[21];

  for (int i = 0; i < 21; i++) {
    resposta[i] = usrClient.read();
  }

  // FP L1
  uint8_t bytesFP1[4] = {
    resposta[9],
    resposta[10],
    resposta[11],
    resposta[12]
  };
  memcpy(&fpIkronL1, bytesFP1, sizeof(float));

  // FP L2
  uint8_t bytesFP2[4] = {
    resposta[13],
    resposta[14],
    resposta[15],
    resposta[16]
  };
  memcpy(&fpIkronL2, bytesFP2, sizeof(float));

  // FP L3
  uint8_t bytesFP3[4] = {
    resposta[17],
    resposta[18],
    resposta[19],
    resposta[20]
  };
  memcpy(&fpIkronL3, bytesFP3, sizeof(float));

  Serial.println("===== FATOR DE POTENCIA iKRON =====");

  Serial.print("FP L1: ");
  Serial.println(fpIkronL1, 3);

  Serial.print("FP L2: ");
  Serial.println(fpIkronL2, 3);

  Serial.print("FP L3: ");
  Serial.println(fpIkronL3, 3);

  usrClient.stop();

  return true;
}

/*float medirRMS(int pino,int amostras){
 float media = 0.0;
  float somaQuadrados = 0.0;
   
    for (int i = 0; i < amostras; i++){
      media += analogRead(pino);
     delayMicroseconds(200);
   }
    media /= amostras;

      for ( int i = 0; i < amostras; i++){
        float amostra = analogRead(pino) - media;
         somaQuadrados += amostra * amostra;
        delayMicroseconds(200);
      }
      return sqrt(somaQuadrados / amostras);
    }*/
      
void setup(){ 

   {lcd.init();
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

   WiFi.mode(WIFI_STA);  
   WiFi.begin(ssid, password);
  Serial.println("Conectando ao Wi-Fi...");

  unsigned long InicioConexaoWiFi = millis();

  while(WiFi.status() != WL_CONNECTED &&
         millis()-InicioConexaoWiFi < 15000){
         delay(500);
         Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED &&
    WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
  
    Serial.println();
    Serial.println("Wi-Fi conectado!");
    Serial.println("Endereço IP:");
    Serial.println(WiFi.localIP());
    }
    else{
      Serial.println();
      Serial.println("Wi-Fi indisponivel.");
      Serial.println("o esp continuara executando asmedições.");
    }
     
       tempoEnergiaAnterior = millis();
       
//=========dados testando conexão USR======//
       Serial.println("Testando conexao com USR...");

if (usrClient.connect(usrIP, usrPort)) {
  Serial.println("USR conectado com sucesso!");
  usrClient.stop();
} else {
  Serial.println("Falha ao conectar no USR.");
}
  
//======DADOS IKRON======///
delay(1000);
lerIkronTeste();
}

void loop() {
  
    bool wifiConectado =  WiFi.status() == WL_CONNECTED &&
    WiFi.localIP() != IPAddress(0, 0, 0, 0);
   
if (!wifiConectado) {

  wifiEstavaConectado = false;

  if (millis() - ultimaTentativaWiFi >= intervaloReconexaoWiFi) {
    ultimaTentativaWiFi = millis();

    Serial.println("Wi-Fi desconectado. Tentando reconectar...");

    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }

} 
else {

  if (!wifiEstavaConectado) {
    wifiEstavaConectado = true;

    Serial.println();
    Serial.println("Wi-Fi conectado!");
    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.localIP());
  }
}    

//===== LEITURA PERIODICA DO iKRON =====//
if (WiFi.status() == WL_CONNECTED) {

  if (millis() - ultimaLeituraIkron >= intervaloIkron) {

    ultimaLeituraIkron = millis();

    lerIkronTeste();
    lerFatorPotenciaIkron();
  }
}
 
  if (millis() - lastMeasure >= interval) {
    lastMeasure = millis();
    

    float rmsTensaoADC_R = medirRMS(pinZMPT_R, 1000);
    float rmsTensaoADC_S = medirRMS(pinZMPT_S, 1000);
    float rmsTensaoADC_T = medirRMS(pinZMPT_T, 1000);
    float rmsCorrenteADC_R = medirRMS(pinSCT_R, 1000);
    float rmsCorrenteADC_S = medirRMS(pinSCT_S, 1000);
    float rmsCorrenteADC_T = medirRMS(pinSCT_T, 1000);

//===== fatores iniciais de calibração tensão e corrente =====

       float fatorTensaoR = 0.4853;
       float fatorTensaoS = 0.4853;
       float fatorTensaoT = 0.4853;

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
      float fatorCorrenteS = 0.039;    
      float fatorCorrenteT = 0.039;             
     
     if (rmsCorrenteADC_R < 25 ) {
      currentR = 0.0;
     }
        else{ currentR = rmsCorrenteADC_R * fatorCorrenteR;        
     }
      if (rmsCorrenteADC_S < 25 ) {
      currentS = 0.0;
     }
        else{ currentS = rmsCorrenteADC_S * fatorCorrenteS;         
     }
      if (rmsCorrenteADC_T < 25 ) {
      currentT = 0.0;
     }
        else{ currentT = rmsCorrenteADC_T * fatorCorrenteT;         
     }

    //===== Potência aparente =====//
          PowerR = voltageR * currentR;
          PowerS = voltageS * currentS;
          PowerT = voltageT * currentT;
      apparentPower = PowerR + PowerS + PowerT;
      energydelta = PowerR + PowerS + PowerT;

   //===== Potência Ativa utilizando fator potencia Ikron======//
         activePower =
          (voltageR * currentR * fpIkronL1)+
          (voltageS * currentS * fpIkronL2)+
          (voltageT * currentT * fpIkronL3);


       //===== Energia aproximada usando tempo real =====//
unsigned long tempoAtualEnergia = millis();
unsigned long deltaTempo = tempoAtualEnergia - tempoEnergiaAnterior;

tempoEnergiaAnterior = tempoAtualEnergia;

energydelta += (apparentPower / 1000.0) * (deltaTempo / 3600000.0); // calculo para transformar em Kwh
/*energyDeltaBackend += (apparentPower / 1000.0) * (deltaTempo / 3600000.0);*/

energy += (activePower / 1000.0) * (interval / 3600000.0); // calculo para transformar em Kwh
energyDeltaBackend += (activePower / 1000.0) * (interval / 3600000.0);

Serial.print("Tempo real entre medicoes: ");
Serial.print(deltaTempo);
Serial.println(" ms");

// ===== Alternância das telas do LCD =====
  if (millis() - ultimoTempoLCD >= intervaloLCD) {
    ultimoTempoLCD = millis();
     telaTensao = !telaTensao;

  lcd.clear();
}
//==Tela LCD das tensões==//
  if (telaTensao) {
  lcd.setCursor(0, 0);
      lcd.print("R:");
         lcd.print(voltageR, 0);
            lcd.print("V  S:");
              lcd.print(voltageS, 0);
                lcd.print("V   ");
              lcd.setCursor(0, 1);
          lcd.print("T:");
      lcd.print(voltageT, 0);
  lcd.print("V  Tensoes  ");
}
else {
// Tela LCD das correntes
  lcd.setCursor(0, 0);
      lcd.print("R:");
        lcd.print(currentR, 1);
           lcd.print("A S:");
            lcd.print(currentS, 1);
              lcd.print("A   ");
            lcd.setCursor(0, 1);
          lcd.print("T:");
      lcd.print(currentT, 1);
  lcd.print("A Correntes ");
}

//===== ENVIO PARA O THINGSBOARD CLOUD =====//
if (millis() - ultimoEnvioThingsboard >= intervaloThingsboard) {
  ultimoEnvioThingsboard = millis();

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient httpThingsboard;    
    httpThingsboard.begin(thingsboardServer);
    httpThingsboard.addHeader("Content-Type", "application/json");
    httpThingsboard.setTimeout(5000);

    // Formato de telemetria do ThingsBoard: só pares chave-valor,
    // sem precisar identificar o dispositivo (o token na URL já faz isso)
    String payloadThingsboard = String("{") +
  "\"tensao_r\":" + String(voltageR, 2) + "," +
  "\"tensao_s\":" + String(voltageS, 2) + "," +
  "\"tensao_t\":" + String(voltageT, 2) + "," +

  "\"ikron_l1\":" + String(tensaoIkronL1, 2) + "," +
  "\"ikron_l2\":" + String(tensaoIkronL2, 2) + "," +
  "\"ikron_l3\":" + String(tensaoIkronL3, 2) + "," +

  "\"corrente_r\":" + String(currentR, 3) + "," +
  "\"corrente_s\":" + String(currentS, 3) + "," +
  "\"corrente_t\":" + String(currentT, 3) + "," +

  "\"energia_deltakwh\":" + String(energydelta, 6) + "," +
  "\"energia_kwh\":" + String(energy, 6) + "," +
  "\"potencia_aparente\":" + String(apparentPower, 2) + "," +
  "\"potencia_ativa\":" + String(activePower, 2) +
  "}";

    int codigoThingsboard = httpThingsboard.POST(payloadThingsboard);

    if (codigoThingsboard == 200) {
      Serial.println("Dados enviados ao ThingsBoard com sucesso.");
    } else {
      Serial.print("Falha ao enviar ao ThingsBoard. Codigo: ");
      Serial.println(codigoThingsboard);
    }

    httpThingsboard.end();
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
    httpBackend.setTimeout(7000);

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
     Serial.print("potência Ativa: ");
       Serial.print(activePower, 2);
        Serial.println(" W"); 
           Serial.print("Potencia aparente: ");
              Serial.print(apparentPower, 2);
              Serial.println(" VA");

            Serial.print("Energia: ");
         Serial.print(energy, 6);
      Serial.println(" kWh");
 Serial.println("===================");
     }
  }
