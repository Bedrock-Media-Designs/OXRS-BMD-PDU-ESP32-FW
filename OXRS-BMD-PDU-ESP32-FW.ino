/**
  ESP32 power distribution unit firmware for the Open eXtensible Rack System
  
  See https://oxrs.io/docs/firmware/pdu-esp32.html for documentation.

  Compile options:
    ESP32

  External dependencies. Install using the Arduino library manager:
    "Adafruit_MCP23017"
    "OXRS-SHA-Rack32-ESP32-LIB" by SuperHouse Automation Pty
    "OXRS-SHA-IOHandler-ESP32-LIB" by SuperHouse Automation Pty

  Compatible with the PDU8 hardware found here:
    https://bmdesigns.com.au/

  GitHub repository:
    https://github.com/Bedrock-Media-Designs/OXRS-BMD-PDU-ESP32-FW
    
  Bugs/Features:
    See GitHub issues list

  Copyright 2019-2022 Bedrock Media Designs Ltd
*/

/*--------------------------- Firmware -----------------------------------*/
#define FW_NAME       "OXRS-BMD-PDU-ESP32-FW"
#define FW_SHORT_NAME "Power Distribution Unit"
#define FW_MAKER      "Bedrock Media Designs"
#define FW_VERSION    "1.0.0-ALPHA2"

/*--------------------------- Libraries ----------------------------------*/
#include <Adafruit_MCP23X17.h>        // For MCP23017 I/O buffers
#include <Adafruit_INA260.h>          // For INA260 current sensors
#include <OXRS_Rack32.h>              // Rack32 support
#include <OXRS_Input.h>               // For input handling
#include <OXRS_Output.h>              // For output handling
#include "logo.h"                     // Embedded maker logo

/*--------------------------- Constants ----------------------------------*/
// Serial
#define       SERIAL_BAUD_RATE      115200

// INA260 setup (should we make this configurable?)
const INA260_AveragingCount DEFAULT_AVERAGING_COUNT = INA260_COUNT_16;
const INA260_ConversionTime DEFAULT_CONVERSION_TIME = INA260_TIME_140_us;

// Can have up to 16x INA260s on a single I2C bus
const byte    INA_I2C_ADDRESS[]     = { 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F };
const uint8_t INA_COUNT             = sizeof(INA_I2C_ADDRESS);

// Define the MCP addresses
const byte    MCP_I2C_ADDRESS[]     = { 0x20, 0x21 };
const uint8_t MCP_COUNT             = sizeof(MCP_I2C_ADDRESS);

#define       MCP_OUTPUT_INDEX      0
#define       MCP_INPUT_INDEX       1

// Each MCP23017 has 16 I/O pins
#define       MCP_PIN_COUNT         16

// Speed up the I2C bus to get faster event handling
#define       I2C_CLOCK_SPEED       400000L

// Default publish telemetry interval (5 seconds)
#define       DEFAULT_PUBLISH_TELEMETRY_MS  5000

/*--------------------------- Global Variables ---------------------------*/
// Each bit corresponds to a device found on the IC2 bus
uint8_t g_inasFound = 0;
uint8_t g_mcpsFound = 0;

// Publish telemetry data interval - extend or disable via the config
// option "publishTelemetrySeconds" - zero to disable
uint32_t g_publishTelemetryMs = DEFAULT_PUBLISH_TELEMETRY_MS;
uint32_t g_lastPublishTelemetry = 0L;

/*--------------------------- Global Objects -----------------------------*/
// Rack32 handler
OXRS_Rack32 rack32(FW_NAME, FW_SHORT_NAME, FW_MAKER, FW_VERSION, FW_LOGO);

// Current sensors
Adafruit_INA260 ina260[INA_COUNT];

// I/O buffers
Adafruit_MCP23X17 mcp23017[MCP_COUNT];

// I/O handlers
OXRS_Output oxrsOutput;
OXRS_Input oxrsInput;

/*--------------------------- Program ------------------------------------*/
/**
  Setup
*/
void setup()
{
  // Startup logging to serial
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.print  (F("FIRMWARE: ")); Serial.println(FW_NAME);
  Serial.print  (F("MAKER:    ")); Serial.println(FW_MAKER);
  Serial.print  (F("VERSION:  ")); Serial.println(FW_VERSION);
  Serial.println(F("========================================"));

  // Start the I2C bus
  Wire.begin();

  // Scan the I2C bus and set up current sensors and I/O buffers
  scanI2CBus();

  // Start Rack32 hardware
  rack32.begin(jsonConfig, jsonCommand);

  // Set up port display
  //rack32.setDisplayPortLayout(g_mcpsFound, PORT_LAYOUT_IO_48);
  
  // Set up config/command schema (for self-discovery and adoption)
  setConfigSchema();
  setCommandSchema();
  
  // Speed up I2C clock for faster scan rate (after bus scan)
  Wire.setClock(I2C_CLOCK_SPEED);
}

/**
  Main processing loop
*/
void loop()
{
  // Let Rack32 hardware handle any events etc
  rack32.loop();

  float mA[INA_COUNT];
  float mV[INA_COUNT];
  float mW[INA_COUNT];
  bool alert[INA_COUNT];

  // Iterate through each of the INA260s found on the I2C bus
  for (uint8_t ina = 0; ina < INA_COUNT; ina++)
  {
    if (bitRead(g_inasFound, ina) == 0)
      continue;

    // Read the values for this sensor
    mA[ina] = ina260[ina].readCurrent();
    mV[ina] = ina260[ina].readBusVoltage();
    mW[ina] = ina260[ina].readPower();
    alert[ina] = ina260[ina].alertFunctionFlag();
    
    // TODO: keep track of total power draw and check thresholds?
  }

  // Publish telemetry data if required
  publishTelemetry(mA, mV, mW, alert);

  // Iterate through each of the MCP23017s found on the I2C bus
  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    if (bitRead(g_mcpsFound, mcp) == 0)
      continue;

    // Check for any output events
    if (mcp == MCP_OUTPUT_INDEX)
    {
      oxrsOutput.process();
    }
    
    // Read the values for all 16 pins on this MCP
    uint16_t io_value = mcp23017[mcp].readGPIOAB();
    
    // Show port animations
    rack32.updateDisplayPorts(mcp, io_value);

    // Check for any input events
    if (mcp == MCP_INPUT_INDEX)
    {
      oxrsInput.process(mcp, io_value);    
    }
  }
}

void publishTelemetry(float mA[], float mV[], float mW[], bool alert[])
{
  // Ignore if publishing has been disabled
  if (g_publishTelemetryMs == 0) { return; }

  // Check if we are ready to publish
  if ((millis() - g_lastPublishTelemetry) > g_publishTelemetryMs)
  {
    DynamicJsonDocument telemetry(1024);
    JsonArray array = telemetry.to<JsonArray>();
   
    for (uint8_t ina = 0; ina < INA_COUNT; ina++)
    {
      if (bitRead(g_inasFound, ina) == 0)
        continue;

      JsonObject json = array.createNestedObject();
      json["index"] = ina + 1;
      json["mA"] = mA[ina];
      json["mV"] = mV[ina];
      json["mW"] = mW[ina];
      json["alert"] = alert[ina];
    }

    // Publish to MQTT
    if (!telemetry.isNull())
    {
      rack32.publishTelemetry(telemetry.as<JsonVariant>());
    }
    
    // Reset our timer
    g_lastPublishTelemetry = millis();
  }
}

/**
  Config handler
 */
void setConfigSchema()
{
  // Define our config schema
  StaticJsonDocument<2048> json;
  JsonVariant config = json.as<JsonVariant>();

  JsonObject publishTelemetrySeconds = config.createNestedObject("publishTelemetrySeconds");
  publishTelemetrySeconds["title"] = "Publish Telemetry (seconds)";
  publishTelemetrySeconds["description"] = "How often to publish sensor data from the onboard INA260 current sensors (defaults to 60 seconds, setting to 0 disables temperature reports). Must be a number between 0 and 86400 (i.e. 1 day).";
  publishTelemetrySeconds["type"] = "integer";
  publishTelemetrySeconds["minimum"] = 0;
  publishTelemetrySeconds["maximum"] = 86400;

  outputConfigSchema(config);

  // Pass our config schema down to the Rack32 library
  rack32.setConfigSchema(config);
}

void outputConfigSchema(JsonVariant json)
{
  JsonObject outputs = json.createNestedObject("outputs");
  outputs["type"] = "array";
  
  JsonObject items = outputs.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject index = properties.createNestedObject("index");
  index["type"] = "integer";
  index["minimum"] = 1;
  index["maximum"] = getMaxIndex();

  // TODO: over current thresholds?

  JsonArray required = items.createNestedArray("required");
  required.add("index");
}

void jsonConfig(JsonVariant json)
{
  if (json.containsKey("publishTelemetrySeconds"))
  {
    g_publishTelemetryMs = json["publishTelemetrySeconds"].as<uint32_t>() * 1000L;
  }

  if (json.containsKey("outputs"))
  {
    for (JsonVariant output : json["outputs"].as<JsonArray>())
    {
      jsonOutputConfig(output);
    }
  }
}

void jsonOutputConfig(JsonVariant json)
{
  uint8_t index = getIndex(json);
  if (index == 0) return;

  // TODO: over current thresholds?
}

/**
  Command handler
 */
void setCommandSchema()
{
  // Define our config schema
  StaticJsonDocument<2048> json;
  JsonVariant command = json.as<JsonVariant>();

  outputCommandSchema(command);
  
  // Pass our command schema down to the Rack32 library
  rack32.setCommandSchema(command);
}

void outputCommandSchema(JsonVariant json)
{
  JsonObject outputs = json.createNestedObject("outputs");
  outputs["type"] = "array";
  
  JsonObject items = outputs.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject index = properties.createNestedObject("index");
  index["type"] = "integer";
  index["minimum"] = 1;
  index["maximum"] = getMaxIndex();

  JsonObject command = properties.createNestedObject("command");
  command["type"] = "string";
  JsonArray commandEnum = command.createNestedArray("enum");
  commandEnum.add("query");
  commandEnum.add("on");
  commandEnum.add("off");

  JsonArray required = items.createNestedArray("required");
  required.add("index");
  required.add("command");
}

void jsonCommand(JsonVariant json)
{
  if (json.containsKey("outputs"))
  {
    for (JsonVariant output : json["outputs"].as<JsonArray>())
    {
      jsonOutputCommand(output);
    }
  }
}

void jsonOutputCommand(JsonVariant json)
{
  uint8_t index = getIndex(json);
  if (index == 0) return;

  if (json.containsKey("command"))
  {
    if (json["command"].isNull() || strcmp(json["command"], "query") == 0)
    {
      // Publish a status event with the current state
      uint8_t state = mcp23017[MCP_OUTPUT_INDEX].digitalRead(index);
      publishOutputEvent(index, RELAY, state);
    }
    else
    {
      // Send this command down to our output handler to process
      if (strcmp(json["command"], "on") == 0)
      {
        oxrsOutput.handleCommand(MCP_OUTPUT_INDEX, index, RELAY_ON);
      }
      else if (strcmp(json["command"], "off") == 0)
      {
        oxrsOutput.handleCommand(MCP_OUTPUT_INDEX, index, RELAY_OFF);
      }
      else 
      {
        Serial.println(F("[pdu ] invalid command"));
      }
    }
  }
}

uint8_t getMaxIndex()
{
  // Remember our indexes are 1-based
  return MCP_PIN_COUNT;  
}

uint8_t getIndex(JsonVariant json)
{
  if (!json.containsKey("index"))
  {
    Serial.println(F("[pdu ] missing index"));
    return 0;
  }
  
  uint8_t index = json["index"].as<uint8_t>();
  
  // Check the index is valid for this device
  if (index <= 0 || index > getMaxIndex())
  {
    Serial.println(F("[pdu ] invalid index"));
    return 0;
  }

  return index;
}

void publishOutputEvent(uint8_t index, uint8_t type, uint8_t state)
{
  char outputType[8];
  getOutputType(outputType, type);
  char outputEvent[7];
  getOutputEventType(outputEvent, state);

  StaticJsonDocument<128> json;
  json["index"] = index;
  json["type"] = outputType;
  json["event"] = outputEvent;

  if (!rack32.publishStatus(json.as<JsonVariant>()))
  {
    Serial.print(F("[pdu ] [failover] "));
    serializeJson(json, Serial);
    Serial.println();

    // TODO: add failover handling code here
  }
}

void getOutputType(char outputType[], uint8_t type)
{
  // Determine what type of event
  sprintf_P(outputType, PSTR("error"));
  switch (type)
  {
    case RELAY:
      sprintf_P(outputType, PSTR("relay"));
      break;
  }
}

void getOutputEventType(char eventType[], uint8_t state)
{
  // Determine what event we need to publish
  sprintf_P(eventType, PSTR("error"));
  switch (state)
  {
    case RELAY_ON:
      sprintf_P(eventType, PSTR("on"));
      break;
    case RELAY_OFF:
      sprintf_P(eventType, PSTR("off"));
      break;
  }
}

/**
  Event handlers
*/
void inputEvent(uint8_t id, uint8_t input, uint8_t type, uint8_t state)
{
  uint8_t outputType = RELAY;
  uint8_t outputState = state == LOW_EVENT ? RELAY_ON : RELAY_OFF;
  
  // Pass this event straight thru to the output handler, using same index
  outputEvent(MCP_OUTPUT_INDEX, input, outputType, outputState);
}

void outputEvent(uint8_t id, uint8_t output, uint8_t type, uint8_t state)
{
  // Update the MCP pin - i.e. turn the relay on/off
  mcp23017[id].digitalWrite(output, state);

  // Publish the event
  publishOutputEvent(output, type, state);
}

/**
  I2C
 */
void scanI2CBus()
{
  // Initialise current sensors
  Serial.println(F("[pdu ] scanning for current sensors..."));

  for (uint8_t ina = 0; ina < INA_COUNT; ina++)
  {
    Serial.print(F(" - 0x"));
    Serial.print(INA_I2C_ADDRESS[ina], HEX);
    Serial.print(F("..."));

    if (ina260[ina].begin(INA_I2C_ADDRESS[ina]))
    {
      bitWrite(g_inasFound, ina, 1);
      Serial.println(F("INA260"));

      // Set the number of samples to average
      ina260[ina].setAveragingCount(DEFAULT_AVERAGING_COUNT);
      
      // Set the time over which to measure the current and bus voltage
      ina260[ina].setVoltageConversionTime(DEFAULT_CONVERSION_TIME);
      ina260[ina].setCurrentConversionTime(DEFAULT_CONVERSION_TIME);
    }
    else
    {
      Serial.println(F("empty"));
    }
  }

  // Initialise I/O buffers
  Serial.println(F("[pdu ] scanning for I/O buffers..."));

  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    Serial.print(F(" - 0x"));
    Serial.print(MCP_I2C_ADDRESS[mcp], HEX);
    Serial.print(F("..."));
  
    Wire.beginTransmission(MCP_I2C_ADDRESS[mcp]);
    if (Wire.endTransmission() == 0)
    {
      bitWrite(g_mcpsFound, mcp, 1);
      Serial.println(F("MCP23017"));
  
      mcp23017[mcp].begin_I2C(MCP_I2C_ADDRESS[mcp]);
      for (uint8_t pin = 0; pin < MCP_PIN_COUNT; pin++)
      {
        mcp23017[mcp].pinMode(pin, mcp == MCP_OUTPUT_INDEX ? OUTPUT : INPUT);
      }
  
      if (mcp == MCP_OUTPUT_INDEX)
      {
        // Initialise the output handler (default to RELAY, not configurable)
        oxrsOutput.begin(outputEvent, RELAY);
      }
      if (mcp == MCP_INPUT_INDEX)
      {
        // Initialise the input handler (default to SWITCH, not configurable)
        oxrsInput.begin(inputEvent, SWITCH);
      }
    }
    else
    {
      Serial.println(F("empty"));
    }
  }
}
