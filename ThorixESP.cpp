#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <AsyncMQTT_ESP32.h>

/*Thorix regulation ESP32
En priorité on s'assure qu'on ne dépasse par la valeur max en depart et que le thermostat d'ambiance donne le go
Ensuite, on vient asservir la temperature de retour plancher en fonction d'une consigne de retour.
Cette temperature de consigne de retour est issue des abaques Thorix : à 20°C ext = 30°C, -10°Cext = 50° confort et 45° à - 10°C en eco
Ce qui nous donne pour simplifier une consigne de retour (loi d'eau) -0,5*Text+ 40
Un hysteresis Haut et bas est donné et sera à ajuster en fonction des caractéristiques de la maison.
*/

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define MQTT_HOST IPAddress(192, 168, 1, 18)
#define MQTT_PORT 1883


// Temperature MQTT Topic
#define MQTT_PUB_TEMP_DEPART "thorix/depart/temperature"
#define MQTT_PUB_TEMP_RETOUR "thorix/retour/temperature"
#define MQTT_PUB_TEMP_EXT "thorix/ext/temperature"
#define MQTT_PUB_TEMP_CONSIGNE "thorix/consigne/temperature"
#define MQTT_PUB_TH_AMBIANCE "thorix/thambiance/status"
#define MQTT_PUB_RELAY_CIRCULATEUR "thorix/CIRCU/status"
#define MQTT_PUB_RELAY_EV "thorix/EV/status"

// Data wire is plugged into port 4 on the Arduino
const int oneWireBus = 4;   
const int inputThAmb = 14;
const int input1 = 12;
const int input2 = 27;
const int RelaiEV = 32;
const int RelaiCircu = 33;
const int RelayChaudiere = 25;
int ThAmbiance,Button1,Button2 = 0;
const double DelaiVeilleEcran = 60*1000;//secondes avant veille ecran
const float TMaxDep = 40;
float TConsigneRetour;//issue de la loi d'eau en fonction de la T° exterieure
float TConsigneRetourCirculateur = 25;//permet d'evacuer la chaleur accumulée dans l'echangeur si th ambiance vaut 0
float THystererisConsigneRetourBas = 7;//on osccille entre consigne - bas et consigne + haut
float THystererisConsigneRetourHaut = 1;
float THystererisCirculateurBas= 2; 
float THystererisCirculateurHaut = 1;
//const int Relay4 = 26;
const int statusled = 23;//non utilisé pour le moment
unsigned long currentMillis,StartTime,now;
bool ecranseteints = false;


String message,result,result2,result3;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64 // OLED height, in pixels



AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

unsigned long previousMillisForMQTT = 0;   // Stores last time temperature was published
const long intervalMQTT = 10000;        // Interval at which to publish sensor readings

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  displayInfos("Connecting to Wi-Fi...","","");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  displayInfos("Connecting to MQTT...","","");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      connectToMqtt();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi lost connection");
      displayInfos("WiFi lost connection","","");
      xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
      xTimerStart(wifiReconnectTimer, 0);
      break;
  }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  displayInfos("Disconnected from MQTT","","");
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

Adafruit_SH1106G oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SSD1306 oled2(SCREEN_WIDTH, 32, &Wire, -1);

OneWire oneWire(oneWireBus);

DallasTemperature sensors(&oneWire);

//int numberOfDevices; // Number of temperature devices found

//DeviceAddress tempDeviceAddress; // We'll use this variable to store a found device address


void displayIntro(){
  oled.clearDisplay();
  oled.setTextSize(1); 
  oled.setCursor(2,30);
  oled.println("BIRCKEL");
  oled.setCursor(10,50);
  oled.setTextSize(2); 
  oled.println("TECH");
  oled.display();
  delay(2000);
}

void displayInfos(String infos1,String infos2,String infos3){
  oled2.clearDisplay();
  oled2.setTextSize(1); 
  oled2.setCursor(0,0);
  oled2.println(infos1);
  oled2.println(infos2);
  oled2.print(infos3);
  oled2.display();
}

void displayTempLine(float th, String text, int offset){  
  oled.setCursor(0,3+offset); 
  oled.setTextSize(1); 
  oled.print(text);
  oled.print(" ");
  oled.setCursor(25,offset); 
  oled.setTextSize(2); 
  oled.print(static_cast<int>(th));
  oled.setTextSize(1);
  oled.setCursor(46,7+offset);
  int result = abs(static_cast<int>(th*10))%10;
  oled.print(".");
  oled.print(result);
  oled.write(0xF8); // Print the degrees symbol
}

void displayText1(float t_dep, float t_ret, float t_ext,float t_cons,bool ThAmb,bool RelayCirc,bool RelayEv ){
  oled.clearDisplay();
  
  displayTempLine(t_dep,"DEP",0);
  displayTempLine(t_ret,"RET",20);
  displayTempLine(t_ext,"EXT",40);
  displayTempLine(t_cons,"Cons",60);
  oled.setCursor(0,80);
  //display.println(); // New line
 
  oled.setTextSize(1); 
  oled.print("Th amb ");
  oled.setTextSize(2); 
  oled.print(ThAmb);
  oled.println(); // New line

  oled.setTextSize(1); 
  oled.print("Circul ");
  oled.setTextSize(2); 
  oled.print(RelayCirc);
  oled.println(); // New line
  
  oled.setTextSize(1); 
  oled.print("EVanne ");
  oled.setTextSize(2); 
  oled.print(RelayEv);
  oled.println(); // New line

  
  oled.display();
   
}

String RegulationEV(float Tret,float HysteresisHaut,float HysteresisBas,int Relai){
  if (Tret >= HysteresisHaut){
    digitalWrite(Relai, LOW);
    message =  "Arret chauffe";
  }
  else if (Tret <= HysteresisBas){
    digitalWrite(Relai, HIGH); 
    message =  "Chauffe";  
  }
  else{
    message =  "Hystereris EV";// °
  }    
  
  return message; 
}

String RegulationCircu(float Tret,float HysteresisHaut,float HysteresisBas,int Relai){
  if (Tret >= HysteresisHaut){
    digitalWrite(Relai, HIGH);
    message =  "Refroidissement";
  }
  else if (Tret <= HysteresisBas){
    digitalWrite(Relai, LOW);
    message =  "Arret circulateur";
  }
  else{
    message =  "Hystereris Circu";// °
  } 

  return message;  
}


/*
Found device 0 with address: 283ECD9B0E0000AF
Found device 1 with address: 283DE29B0E000061
Found device 2 with address: 28FF641F758F594E
*/

//on n' utilise pas les fonction de découverte automatique ça évite de croiser les sondes
DeviceAddress SensorDepart = {0x28, 0x3E, 0xCD, 0x9B, 0x0E, 0x00, 0x00, 0xAF};
DeviceAddress SensorRetour = {0x28, 0x3D, 0xE2, 0x9B, 0x0E, 0x00, 0x00, 0x61};
DeviceAddress SensorExt = {0x28, 0xFF, 0x64, 0x1F, 0x75, 0x8F, 0x59, 0x4E};

void setup(void) {
  // start serial port
  Serial.begin(115200);

  oled.begin(0x3D,true);

  if (!oled2.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("failed to start SSD1306 OLED 2"));
    while (1);
  }
  
  oled.setRotation(1);
  oled.cp437(true);
  oled.setTextColor(WHITE);    // La couleur du texte
  displayIntro();

  oled2.clearDisplay(); // clear display
  oled2.cp437(true);
  oled2.setTextSize(1);         // set text size
  oled2.setTextColor(WHITE);    // set text color
  //oled2.setCursor(0, 2);       // set position to display (x,y)

  // Start up the library
  sensors.begin();

  pinMode(inputThAmb, INPUT);   //external pull up
  pinMode(input1, INPUT_PULLUP); 
  pinMode(input2, INPUT_PULLUP); 
  pinMode(RelaiEV, OUTPUT);  
  pinMode(RelaiCircu, OUTPUT);  
 // pinMode(Relay4, OUTPUT);  
  pinMode(statusled, OUTPUT);    



 
  // Grab a count of devices on the wire
  /*

  // function to print a device address
  void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
      Serial.print(deviceAddress[i], HEX);
    }
  }


  numberOfDevices = sensors.getDeviceCount();
  
  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(numberOfDevices, DEC);
  Serial.println(" devices.");

  // Loop through each device, print out address
  
  for(int i=0;i<numberOfDevices; i++) {
    // Search the wire for address
    if(sensors.getAddress(tempDeviceAddress, i)) {
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(tempDeviceAddress);
      Serial.println();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }
    
  }
  */
    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
    wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

    WiFi.onEvent(WiFiEvent);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    //mqttClient.onSubscribe(onMqttSubscribe);
    //mqttClient.onUnsubscribe(onMqttUnsubscribe);
    mqttClient.onPublish(onMqttPublish);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCredentials("mqttuser", "mqttuser");
    connectToWifi();

    displayInfos("Thorix ESP",WiFi.localIP().toString(),"");
    
    StartTime = millis();//veille ecrans
    digitalWrite(RelaiCircu, HIGH);//Quelque soit l'etat précedent on démarre le circulateur
}

void loop(void) { 
  //gestion veille ecrans
  now = millis();
  if (now - StartTime > DelaiVeilleEcran){//devrait marcher apres 49,7 jours, gestion veille ecrans
        if (Button1 == HIGH || Button2 == HIGH){
            ecranseteints = false;
            StartTime = millis();
        }
        else{
            ecranseteints = true;
        }
  }
  
  if (ecranseteints){
      oled.clearDisplay();
      oled2.clearDisplay();
      oled.display();
      oled2.display();
  }
  
 
  sensors.requestTemperatures(); 

  //float Tdep = sensors.getTempCByIndex(0);
  float Tdep = sensors.getTempC(SensorDepart);

  //float Tret = sensors.getTempCByIndex(1);
  float Tret = sensors.getTempC(SensorRetour);

  //float Text = sensors.getTempCByIndex(2);
  float Text = sensors.getTempC(SensorExt);

  Serial.println("Temperature depart");
  Serial.print(Tdep);
  Serial.println("ºC");

  Serial.println("Temperature retour");
  Serial.print(Tret);
  Serial.println("ºC");

  Serial.println("Temperature exterieure");
  Serial.print(Text);
  Serial.println("ºC");
  
  TConsigneRetour = (-0.5 * Text) + 32 ;//en pratique il semblerait que les tuyaux soient proches des carreaux, une consigne trop elevée engendre des temperatures sol > 25°, ce qui n'est plus recommandé
 

  Button1 = !digitalRead(input1);
  Button2 = !digitalRead(input2);

  ThAmbiance = !digitalRead(inputThAmb);//lecture du thermostat d'ambiance

  
  if (Tdep > -10 && Tret > -10 && Text > -30){//on considere que sous ces valeurs les sondes sont HS

    if(ThAmbiance == HIGH && Tdep < TMaxDep){
        
        digitalWrite(RelaiCircu, HIGH);//on vient forcer le demarrage du circulateur
        
        result = RegulationEV(Tret,(TConsigneRetour + THystererisConsigneRetourHaut),(TConsigneRetour - THystererisConsigneRetourBas),RelaiEV);//regulation de l'electrovanne 
        result2 = TConsigneRetour - THystererisConsigneRetourBas;    
        result3= TConsigneRetour + THystererisConsigneRetourHaut;      
    }
    else{

        digitalWrite(RelaiEV, LOW); //on ferme l'electrovanne si le thermostat d'ambiance ne demande plus la chauffe
        
        if(ThAmbiance == LOW){ //Lancement régulation circulateur
          result = RegulationCircu(Tret,(TConsigneRetourCirculateur + THystererisCirculateurHaut),(TConsigneRetourCirculateur - THystererisCirculateurBas),RelaiCircu);
          result2 = TConsigneRetourCirculateur - THystererisCirculateurBas;    
          result3= TConsigneRetourCirculateur + THystererisCirculateurHaut;   
        }
        else{
          result = "ATTENTION";
          result2 = "Temp depart sup a";
          result3 =  TMaxDep;
          displayInfos(result,result2,result3);//message prioritaire
        }   
    }

    if (!ecranseteints)
        displayInfos(result,result2,result3);  
  }
  else{
      displayInfos("ERREUR SONDE","Arret d'urgence","Verifier le cablage");
      digitalWrite(RelaiEV, LOW);
      digitalWrite(RelaiCircu, HIGH);//permet d'ecouler la chaleur si on etait en mode veille,message prioritaire
  }

  if (!ecranseteints)
    displayText1(Tdep,Tret,Text,TConsigneRetour,ThAmbiance,digitalRead(RelaiCircu),digitalRead(RelaiEV));

  //poublication MQTT
 currentMillis = millis();

  if (currentMillis - previousMillisForMQTT >= intervalMQTT) {
    previousMillisForMQTT = currentMillis;

    sensors.requestTemperatures(); 


    // Publish an MQTT message on topic esp32/ds18b20/temperature
    mqttClient.publish(MQTT_PUB_TEMP_DEPART, 1, true, String(Tdep).c_str());  
    mqttClient.publish(MQTT_PUB_TEMP_RETOUR, 1, true, String(Tret).c_str());    
    mqttClient.publish(MQTT_PUB_TEMP_EXT, 1, true, String(Text).c_str());
    mqttClient.publish(MQTT_PUB_TEMP_CONSIGNE, 1, true, String(TConsigneRetour).c_str());  
    mqttClient.publish(MQTT_PUB_TH_AMBIANCE, 1, true, String(ThAmbiance).c_str());  
    mqttClient.publish(MQTT_PUB_RELAY_CIRCULATEUR, 1, true, String(digitalRead(RelaiCircu)).c_str());  
    mqttClient.publish(MQTT_PUB_RELAY_EV, 1, true, String(digitalRead(RelaiEV)).c_str());                                
   
  }
  delay(1000);
}
