#include "symbols.h"

/**
 * adapted from palto42's code for the ssd1306 display https://github.com/palto42/komoot-navi
 */

#include "BLEDevice.h"
//SP
//#define ENABLE_GxEPD2_GFX 0
//#include <GxEPD2_BW.h> // including both doesn't hurt

//SP
#define LILYGO_T5_V213
#include <boards.h>
#include "esp_adc_cal.h"
#include <driver/adc.h>
#include <GxEPD.h>
#include <GxDEPG0213BN/GxDEPG0213BN.h>    // 2.13" b/w  form DKE GROUP
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

// copy constructor for your e-paper from GxEPD2_Example.ino, and for AVR needed #defines
#define MAX_DISPLAY_BUFFER_SIZE 800 // 
#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

//SP
//GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(GxEPD2_213_B74(/*CS=5*/ 5, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4)); // GDEH0213B74
GxIO_Class io(SPI,  EPD_CS, EPD_DC,  EPD_RSET);
GxEPD_Class display(io, EPD_RSET, EPD_BUSY);


std::string value = "Start";
int timer = 0 ;
// The remote service we wish to connect to.
static BLEUUID serviceUUID("71C1E128-D92F-4FA8-A2B2-0F171DB3436C");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("503DD605-9BCB-4F6E-B235-270A57483026");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

int16_t connectTextBoundsX, connectTextBoundsY;
uint16_t connectTextBoundsW, connectTextBoundsH;
const char connectText[] = "Waiting...";

int16_t connectedTextBoundsX, connectedTextBoundsY;
uint16_t connectedTextBoundsW, connectedTextBoundsH;
const char connectedText[] = "Connected";

const int battPin = 35; // A2=2 A6=34
unsigned int raw=0;
float volt=0.0;
// ESP32 ADV is a bit non-linear
const float vScale1 = 579; // divider for higher voltage range
const float vScale2 = 689; // divider for lower voltage range

long interval = 60000;  // interval to display battery voltage
long previousMillis = 0; // used to time battery update

// distance and streets
std::string old_street = ""; 
uint8_t dir = 255;
uint32_t dist2 = 4294967295;
std::string street;
std::string firstWord;
std::string old_firstWord;
bool updated;

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
//  Serial.print("Notify callback for characteristic ");
//  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
//  Serial.print(" of data length ");
//  Serial.println(length);
//  Serial.print("data: ");
//  Serial.println((char*)pData);
}

class MyClientCallback : public BLEClientCallbacks {

  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

bool connectToServer() {

  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());
  
  BLEClient* pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");


  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if(pRemoteCharacteristic->canRead()) {
    std::string value = pRemoteCharacteristic->readValue();
    if(pRemoteCharacteristic->canNotify()) {
      pRemoteCharacteristic->registerForNotify(notifyCallback);
      Serial.println(" - Registered for notifications");
      
      connected = true;
      return true;
      display.fillRect(0, 0, GxEPD_WIDTH, GxEPD_HEIGHT,GxEPD_WHITE );
    }
    Serial.println("Failed to register for notifications");
  } else {
    Serial.println("Failed to read our characteristic");
  }
  
  pClient->disconnect();
  return false;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;
    } 
  }
}; 

void showPartialUpdate_dir(uint8_t dir) {
    display.fillRect(0, 0, 65, 65, GxEPD_WHITE);
    display.drawBitmap(5, 5, symbols[dir].bitmap, 60, 60, 0);
//    display.update();
    display.updateWindow(0, 0, 65, 65, true);
}

void showPartialUpdate_street(std::string street, std::string old_street ) {
//  if (street.size() > 8) {
    display.setFont(&FreeSansBold9pt7b);
//  } else {
//    display.setFont(&FreeSansBold12pt7b);
//  }
    display.setTextColor(GxEPD_BLACK);
    display.fillRect(10, 70, 239, 51, GxEPD_WHITE);
    display.setCursor(10, 115);
    display.print(old_street.c_str());
    display.setCursor(10, 95);
    display.print(street.c_str());
    display.updateWindow(10, 70, 239, 51, true);
//    display.update();

}

void showPartialUpdate_dist(uint32_t dist) {
  display.setFont(&FreeSansBold18pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.fillRect    (105, 33, 144, 25, GxEPD_WHITE);
  display.setCursor(105, 57);
  display.print(dist);
  display.print("m");
  display.updateWindow(105, 33, 144, 25, true);

}

void getVolts(void * parameter) {
for(;;){ // infinite loop
  raw  = (float) analogRead(battPin);
  //volt = ((float)raw / 4095.0) * 2.0 * 3.3 * (1100 / 1000.0);
  volt = raw / vScale1;
  Serial.print ("Battery = ");
  Serial.println (volt);
  Serial.print ("Raw = ");
  Serial.println (raw);
//  display.fillRect (200, 0, 49, 15, GxEPD_WHITE);
  display.fillRect (66, 0, 184, 16, GxEPD_WHITE);
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(200,15);
  display.print(volt);
  display.print("V");
//  display.update();
//  display.updateWindow(200, 0, 49, 15);
  display.updateWindow(66, 0, 184, 16);
  delay(56*1000);
}
}

void setup() {
  // enable debug output
  Serial.begin(115200);

    // Battery voltage
  pinMode(battPin, INPUT);
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC_ATTEN_DB_2_5, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
  raw  = analogRead(battPin);
  volt = raw / vScale1;
  Serial.print ("Battery = ");
  Serial.println (volt);
  Serial.print ("Raw = ");
  Serial.println (raw);
  
  SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI);
  // Display start
  display.init();
  display.setTextColor(GxEPD_BLACK);
  display.setRotation(3); //orientation set to 1 to flip the display
  display.fillRect(0, 0, 250, 122, GxEPD_WHITE);
  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(40,50);
  display.print("Komoot Nav"); //Boot Image
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(35, 100);
  display.print(connectText);
  display.update();
//  display.setCursor(200,20);
//  display.print(volt);
//  display.print("V");
 
//  xTaskCreate(getVolts,    // Function that should be called
//    "Display BatteryV",   // Name of the task (for debugging)
//    1000,            // Stack size (bytes)
//    NULL,            // Parameter to pass
//    1,               // Task priority
//    NULL             // Task handle
//  );
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("epkm");
  // Display end
  
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(20, false);

  // Battery voltage
  pinMode(battPin, INPUT);
    raw  = (float) analogRead(battPin);
    volt = raw / vScale1;
    Serial.print ("Battery = ");
    Serial.println (volt);
    Serial.print ("Raw = ");
    Serial.println (raw);
    display.fillRect (200, 0, 49, 15, GxEPD_WHITE);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(200,15);
    display.print(volt);
    display.print("V");
//  display.update();
    display.updateWindow(200, 0, 49, 15);
} // End of setup.



void loop() {
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
      //display connected status
      //display.setFont(&FreeSansBold18pt7b);
      //display.fillRect(connectTextBoundsX, connectTextBoundsY, connectTextBoundsW, connectTextBoundsH, GxEPD_BLACK);
      //display.setCursor(35, 100);
      //display.print(connectedText);
      // delay(500);
      display.fillRect(0, 0, 250, 122, GxEPD_WHITE);
      display.update();
     } else {
      Serial.println("We have failed to connect to the server; there is nothing more we will do.");
    }
    doConnect = false;
  }

  
  unsigned long currentMillis = millis();
 
  if(currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;   
    raw  = (float) analogRead(battPin);
    volt = raw / vScale1;
    Serial.print ("Battery = ");
    Serial.println (volt);
    Serial.print ("Raw = ");
    Serial.println (raw);
    display.fillRect (200, 0, 49, 15, GxEPD_WHITE);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(200,15);
    display.print(volt);
    display.print("V");
//  display.update();
    display.updateWindow(200, 0, 49, 15);
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
    std::string value = pRemoteCharacteristic->readValue();//this crashes sometimes, receives the whole data packet
    if (value.length() > 4) {
      //in case we have update flag but characteristic changed due to navigation stop between
      updated = false;


      street = value.substr(9);//this causes abort when there are not at least 9 bytes available
      if (street != old_street) {
        old_street = street;
        old_firstWord = firstWord;
        firstWord = street.substr(0, street.find(", "));
        showPartialUpdate_street(firstWord, old_firstWord);
        Serial.print("Street update: ");
        Serial.println(firstWord.c_str());
        updated = true;
      } //extracts the firstword of the street name and displays it

      std::string direction;
      direction = value.substr(4,4);
      uint8_t d=direction[0];
      if (d != dir) {
        dir = d;
        showPartialUpdate_dir(dir);
        Serial.print("Direction update: ");
        Serial.println(d);
        updated = true;
      } //display direction

      std::string distance;
      distance = value.substr(5,8);
      uint32_t dist=distance[0] | distance[1] << 8 | distance[2] << 16 | distance[3] << 24;
      if (dist2 != dist) {
        // speed calc todo
        //if (dist < dist2) {
        //  uint8_t 
        //}
        dist2 = dist;
                showPartialUpdate_dist(dist2);
       // showPartialUpdate_street(firstWord);
       // showPartialUpdate_dir(dir);
        
        Serial.print("Distance update: ");
        Serial.println(dist2);
        updated = true;
      } //display distance in metres

     if (dist2 > 100) {
        esp_sleep_enable_timer_wakeup(4000000);
        delay(4000);
      } else {
        delay(2000); // Delay between loops.
      }
    } else if (doScan) {
      BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
    }
  }
} // End of loop
