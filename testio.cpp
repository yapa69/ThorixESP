#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <Wire.h>
 
#include <OneWire.h>
#include <DallasTemperature.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64 // OLED height, in pixels

Adafruit_SH1106G oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
//Adafruit_SSD1306 oled(SCREEN_WIDTH, 32, &Wire, -1);

  const int RelaiEV = 19;
const int RelaiCircu = 18;
const int inputThAmb = 4;
const int SW1 = 13;
const int SW2 = 25;


void displayIntro(){
  oled.clearDisplay();
  oled.setTextSize(1); 
  oled.setCursor(2,30);
  oled.println("MODE DEBUG");
  oled.setCursor(10,50);
  oled.setTextSize(1); 
  oled.println("Ouvrir moniteur serie");
  oled.display();
  //delay(2000);
}

// Data wire is conntec to the Arduino digital pin 5 for esp Thosrix
#define ONE_WIRE_BUS 5

int numberOfDevices; // Number of temperature devices found

DeviceAddress tempDeviceAddress; // We'll use this variable to store a found device address

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

 // function to print a device address
  void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
      Serial.print(deviceAddress[i], HEX);
    }
  }


void setup(void) {

 
Serial.begin(115200);
  oled.begin(0x3D,true);


  oled.setRotation(1);
  oled.cp437(true);
  oled.setTextColor(WHITE);    // La couleur du texte
  displayIntro();



  pinMode(RelaiEV, OUTPUT);  
  pinMode(RelaiCircu, OUTPUT);  
  pinMode(inputThAmb, INPUT);   //external pull up
   pinMode(SW1, INPUT_PULLUP);   //internal pull up
    pinMode(SW2, INPUT_PULLUP);   //internal pull up
sensors.begin();
 
}
void loop() {


 int sensorValue = !digitalRead(inputThAmb);//pullUP on inverse
  Serial.println("----Entrees----");

    Serial.println("Th ambiance");
    Serial.println(sensorValue);
    Serial.println("Bouton +");
     int sensorSW1 = !digitalRead(SW1);
    Serial.println(sensorSW1);
     Serial.println("Bouton -");
     int sensorSW2 = !digitalRead(SW2);
    Serial.println(sensorSW2);
    Serial.println("----");

 Serial.println("Activation relai electrovanne pendant 10s");
   digitalWrite(RelaiEV, HIGH);
   delay(10000);
   digitalWrite(RelaiEV, LOW);
   delay(1000);
   Serial.println("Activation relai Circulateur pendant 10s");
   digitalWrite(RelaiCircu, HIGH);  
   delay(10000);
    digitalWrite(RelaiCircu, LOW);
    delay(1000);
     digitalWrite(RelaiEV, HIGH);
      digitalWrite(RelaiCircu, HIGH);
      delay(10000);
      digitalWrite(RelaiEV, LOW);
      digitalWrite(RelaiCircu, LOW);
      delay(1000);

  sensors.requestTemperatures(); 
  
  Serial.println("----Celsius temperature: ");
  // Why "byIndex"? You can have more than one IC on the same bus. 0 refers to the first IC on the wire
  Serial.print(sensors.getTempCByIndex(0)); 
  Serial.print(" - Fahrenheit temperature: ");
  Serial.println(sensors.getTempFByIndex(0));

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

      //oled.setCursor(0,0);
       // oled.println("device adress");
  //oled.println(tempDeviceAddress);
  //oled.display();

    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }
    
  }



    delay(5000);
    

}
