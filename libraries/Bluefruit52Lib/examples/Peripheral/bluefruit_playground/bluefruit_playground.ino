/*********************************************************************
 This is an example for our nRF52 based Bluefruit LE modules

 Pick one up today in the adafruit shop!

 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

/* This sketch demonstrate the BLE Adafruit Service that is used with
 * "Adafruit Bluefruit Playground" app. Supported boards are
 *  - Circuit Playground Bluefruit : https://www.adafruit.com/product/4333
 *  - CLUE nRF52840 : https://www.adafruit.com/product/4500
 *  - Feather Sense : https://www.adafruit.com/product/4516
 */

#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <bluefruit.h>
#include <BLEAdafruitService.h>

// BLE Service
BLEDfu  bledfu;  // OTA DFU service
BLEDis  bledis;  // device information
BLEUart bleuart; // uart over ble
BLEBas  blebas;  // battery

// Adafruit Service: ADAFxx-C332-42A8-93BD-25E905756CB8
BLEAdafruitTemperature  bleTemp;
BLEAdafruitAccel        bleAccel;
BLEAdafruitLightSensor  bleLight;
BLEAdafruitButton       bleButton;
BLEAdafruitTone         bleTone;

BLEAdafruitAddressablePixel     blePixel;

//------------- Circuit Playground Bluefruit -------------//
#if defined(ARDUINO_NRF52840_CIRCUITPLAY)

#include <Adafruit_CircuitPlayground.h>

#define DEVICE_NAME       "CPlay"
#define NEOPIXEL_COUNT    10

uint16_t measure_temperature(uint8_t* buf, uint16_t bufsize)
{
  float temp = CircuitPlayground.temperature();
  memcpy(buf, &temp, 4);
  return 4;
}

uint16_t measure_light(uint8_t* buf, uint16_t bufsize)
{
  float lux;
  lux = CircuitPlayground.lightSensor();
  memcpy(buf, &lux, 4);
  return 4;
}

uint16_t measure_button(uint8_t* buf, uint16_t bufsize)
{
  uint32_t button = 0;

  button |= ( CircuitPlayground.slideSwitch() ? 0x01 : 0x00 );
  button |= ( CircuitPlayground.leftButton()  ? 0x02 : 0x00 );
  button |= ( CircuitPlayground.rightButton() ? 0x04 : 0x00 );

  memcpy(buf, &button, 4);
  return 4;
}

//------------- CLUE & Feather Sense -------------//
#elif defined(ARDUINO_NRF52840_CLUE) || defined(ARDUINO_NRF52840_FEATHER_SENSE)

#include <Adafruit_APDS9960.h>
#include <Adafruit_LSM6DS33.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SHT31.h>

#include <Adafruit_SPIFlash.h>
#include <Adafruit_AHRS.h>
#include <Adafruit_Sensor_Calibration.h>

#if defined(ARDUINO_NRF52840_CLUE)
  #define DEVICE_NAME     "CLUE"
#else
  #define DEVICE_NAME     "Sense"
#endif

#define NEOPIXEL_COUNT    1

BLEAdafruitHumid      bleHumid;
BLEAdafruitBaro       bleBaro;
BLEAdafruitQuaternion bleQuater;

Adafruit_LSM6DS33 lsm6ds33; // Gyro and Accel
Adafruit_LIS3MDL  lis3mdl;  // Magnetometer

Adafruit_APDS9960 apds9960; // Proximity, Light, Gesture, Color
Adafruit_BMP280   bmp280;   // Temperature, Barometric
Adafruit_SHT31    sht30;    // Humid


// pick your filter! slower == better quality output
//Adafruit_NXPSensorFusion filter; // slowest
//Adafruit_Madgwick filter;  // faster than NXP
Adafruit_Mahony filter;  // fastest/smalleset

// Sensor calibration
#define FILE_SENSOR_CALIB       "sensor_calib.json"
Adafruit_Sensor_Calibration_SDFat cal;

Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash flash(&flashTransport);
FatFileSystem fatfs;

uint16_t measure_light(uint8_t* buf, uint16_t bufsize)
{
  float lux;

  uint16_t r, g, b, c;
  apds9960.getColorData(&r, &g, &b, &c);

  lux = c;

  memcpy(buf, &lux, 4);
  return 4;
}

uint16_t measure_button(uint8_t* buf, uint16_t bufsize)
{
  // Button is active LOW on most board except CPlay
  // No slide switch

  uint32_t button = 0;
  button |= ( digitalRead(PIN_BUTTON1) ? 0x00 : 0x02 );
  button |= ( digitalRead(PIN_BUTTON2) ? 0x00 : 0x04 );

  memcpy(buf, &button, 4);
  return 4;
}

uint16_t measure_humid(uint8_t* buf, uint16_t bufsize)
{
  float humid = sht30.readHumidity();
  memcpy(buf, &humid, 4);
  return 4;
}

#else
  #error "Board is not supported"
#endif

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NEOPIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

//------------- Setup -------------//
void setup()
{
  Adafruit_Sensor* accel_sensor;

  Serial.begin(115200);
//  while(!Serial) delay(10); // wait for native USB

#if defined ARDUINO_NRF52840_CIRCUITPLAY
  CircuitPlayground.begin();

  accel_sensor = &CircuitPlayground.lis;
#else

  // Button
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);

  // Buzzer Speaker
  pinMode(PIN_BUZZER, OUTPUT);

  apds9960.begin();
  apds9960.enableColor(true);

  bmp280.begin();
  sht30.begin(0x44);
  lsm6ds33.begin_I2C();
  lis3mdl.begin_I2C();

  // set lowest range
  lsm6ds33.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);
  lsm6ds33.setGyroRange(LSM6DS_GYRO_RANGE_250_DPS);
  lis3mdl.setRange(LIS3MDL_RANGE_4_GAUSS);

  // set slightly above refresh rate
  lsm6ds33.setAccelDataRate(LSM6DS_RATE_104_HZ);
  lsm6ds33.setGyroDataRate(LSM6DS_RATE_104_HZ);
  lis3mdl.setDataRate(LIS3MDL_DATARATE_1000_HZ);
  lis3mdl.setPerformanceMode(LIS3MDL_MEDIUMMODE);
  lis3mdl.setOperationMode(LIS3MDL_CONTINUOUSMODE);

  // Increase I2C speed to 400 Khz
  Wire.setClock(400000);

  accel_sensor = lsm6ds33.getAccelerometerSensor();

  // Init flash, filesystem and calibration & load calib json
  flash.begin();
  fatfs.begin(&flash);
  cal.begin(FILE_SENSOR_CALIB, &fatfs);
  cal.loadCalibration();

  PRINT_FLOAT(cal.mag_hardiron[0]); PRINT_FLOAT(cal.mag_hardiron[1]); PRINT_FLOAT(cal.mag_hardiron[2]);
  PRINT_FLOAT(cal.mag_field);

  PRINT_FLOAT(cal.mag_softiron[0]);PRINT_FLOAT(cal.mag_softiron[1]); PRINT_FLOAT(cal.mag_softiron[2]);
  PRINT_FLOAT(cal.mag_softiron[3]); PRINT_FLOAT(cal.mag_softiron[4]); PRINT_FLOAT(cal.mag_softiron[5]);
  PRINT_FLOAT(cal.mag_softiron[6]); PRINT_FLOAT(cal.mag_softiron[7]); PRINT_FLOAT(cal.mag_softiron[8]);
#endif

  Serial.println("Bluefruit Playground Example");
  Serial.println("---------------------------\n");

  // Setup the BLE LED to be enabled on CONNECT
  // Note: This is actually the default behaviour, but provided
  // here in case you want to control this LED manually via PIN 19
  Bluefruit.autoConnLed(false);

  // Config the peripheral connection with maximum bandwidth 
  // more SRAM required by SoftDevice
  // Note: All config***() function must be called before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

  Bluefruit.begin();
  Bluefruit.setTxPower(8);    // Check bluefruit.h for supported values
  Bluefruit.setName(DEVICE_NAME);
  //Bluefruit.setName(getMcuUniqueID()); // useful testing with multiple central connections
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // To be consistent OTA DFU should be added first if it exists
  bledfu.begin();

  // Configure and Start Device Information Service
  bledis.setManufacturer("Adafruit Industries");
  bledis.begin();

  // Configure and Start BLE Uart Service
  bleuart.begin();

  // Start BLE Battery Service
  blebas.begin();
  blebas.write(100);

  //------------- Adafruit Service -------------//
  bleLight.begin(measure_light);

  bleButton.begin(measure_button, 100);
  bleButton.setPeriod(0); // only notify if there is changes with buttons

  bleTone.begin(PIN_BUZZER);

  strip.begin();
  blePixel.begin(&strip);

  bleAccel.begin(accel_sensor);

  // CPB doesn't support these on-board sensor
#ifdef ARDUINO_NRF52840_CIRCUITPLAY
  bleTemp.begin(measure_temperature);

#else
  bleTemp.begin(bmp280.getTemperatureSensor());
  bleHumid.begin(measure_humid);
  bleBaro.begin(bmp280.getPressureSensor());

  // Quaternion with sensor calibration
  bleQuater.begin(&filter, accel_sensor, lsm6ds33.getGyroSensor(), &lis3mdl);
  bleQuater.setCalibration(&cal);
#endif

  // Set up and start advertising
  startAdv();

  Serial.println("Please use Adafruit's Bluefruit LE app to connect in UART mode");
  Serial.println("Once connected, enter character(s) that you wish to send");
}

void startAdv(void)
{
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);

  // Advertising with only board ID
  struct ATTR_PACKED {
    uint16_t mfr_id;
    
    uint8_t  field_len;
    uint16_t field_key;
    uint16_t field_value;
  } mfr_adv;

  mfr_adv.mfr_id = UUID16_COMPANY_ID_ADAFRUIT;
  mfr_adv.field_len = 4;
  mfr_adv.field_key = 1; // board id
  mfr_adv.field_value = USB_PID;

  Bluefruit.Advertising.addManufacturerData(&mfr_adv, sizeof(mfr_adv));

  // Add name to advertising, since there is enough room
  Bluefruit.Advertising.addName();
  
  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds  
}

void loop()
{

}

// callback invoked when central connects
void connect_callback(uint16_t conn_handle)
{
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));

  Serial.print("Connected to ");
  Serial.println(central_name);
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle connection where this event happens
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
  (void) conn_handle;
  (void) reason;

  Serial.println();
  Serial.print("Disconnected, reason = 0x"); Serial.println(reason, HEX);
}
