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
#define FW_VERSION    "1.0.0"

/*--------------------------- Libraries ----------------------------------*/
#include <Adafruit_MCP23X17.h>        // For MCP23017 I/O buffers
#include <OXRS_Rack32.h>              // Rack32 support
#include <OXRS_Input.h>               // For input handling
#include <OXRS_Output.h>              // For output handling
#include "logo.h"                     // Embedded maker logo

/*--------------------------- Constants ----------------------------------*/
// Serial
#define       SERIAL_BAUD_RATE      115200

// Define the MCP addresses
#define       MCP_OUTPUT_I2C_ADDR   0x20
#define       MCP_INPUT_I2C_ADDR    0x21

// Each MCP23017 has 16 I/O pins
#define       MCP_PIN_COUNT         16

// Speed up the I2C bus to get faster event handling
#define       I2C_CLOCK_SPEED       400000L

/*--------------------------- Global Variables ---------------------------*/
bool mcpOutputFound = false;
bool mcpInputFound  = false;

/*--------------------------- Global Objects -----------------------------*/
// Rack32 handler
OXRS_Rack32 rack32(FW_NAME, FW_SHORT_NAME, FW_MAKER, FW_VERSION, FW_LOGO);

// I/O buffers
Adafruit_MCP23X17 mcpOutput;
Adafruit_MCP23X17 mcpInput;

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

  // Scan the I2C bus and set up I/O buffers
  scanI2CBus();

  // Start Rack32 hardware
  rack32.begin(jsonConfig, jsonCommand);

  // Set up port display
  //rack32.setDisplayPortLayout(g_mcps_found, PORT_LAYOUT_IO_48);
  
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

  if (mcpOutputFound)
  {
    // Check for any output events
    oxrsOutput.process();
    
    // Read the values for all 16 pins on this MCP
    uint16_t io_value = mcpOutput.readGPIOAB();
  
    // Show port animations
    rack32.updateDisplayPorts(0, io_value);
  }

  if (mcpInputFound)
  {
    // Read the values for all 16 pins on this MCP
    uint16_t io_value = mcpInput.readGPIOAB();

    // Check for any input events
    oxrsInput.process(0, io_value);    
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

  if (mcpOutputFound)
  {
    outputConfigSchema(config);
  }

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

  if (mcpOutputFound)
  {
    outputCommandSchema(command);
  }
  
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
      uint8_t state = mcpOutput.digitalRead(index);
      publishOutputEvent(index, RELAY, state);
    }
    else
    {
      // Send this command down to our output handler to process
      if (strcmp(json["command"], "on") == 0)
      {
        oxrsOutput.handleCommand(0, index, RELAY_ON);
      }
      else if (strcmp(json["command"], "off") == 0)
      {
        oxrsOutput.handleCommand(0, index, RELAY_OFF);
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
  outputEvent(id, input, outputType, outputState);
}

void outputEvent(uint8_t id, uint8_t output, uint8_t type, uint8_t state)
{
  // Update the MCP pin - i.e. turn the relay on/off
  mcpOutput.digitalWrite(output, state);

  // Publish the event
  publishOutputEvent(output, type, state);
}

/**
  I2C
 */
void scanI2CBus()
{
  Serial.println(F("[pdu ] scanning for I/O buffers..."));

  // Initialise I/O buffers
  mcpOutputFound = initialiseMCP23017(&mcpOutput, MCP_OUTPUT_I2C_ADDR, OUTPUT);
  mcpInputFound =  initialiseMCP23017(&mcpInput,  MCP_INPUT_I2C_ADDR,  INPUT);

  // Initialise the output handler (default to RELAY, not configurable)
  if (mcpOutputFound)
  {
    oxrsOutput.begin(outputEvent, RELAY);
  }
  else
  {
    Serial.println(F("[pdu ] no output I/O detected, unable to control relays"));
  }

  // Initialise the input handler (default to SWITCH, not configurable)
  if (mcpInputFound)
  {
    oxrsInput.begin(inputEvent, SWITCH);
  }
  else
  {
    Serial.println(F("[pdu ] no input I/O detected, unable to switch relays manually"));
  }
}

bool initialiseMCP23017(Adafruit_MCP23X17 * mcp, int address, int pinMode)
{
  Serial.print(F(" - 0x"));
  Serial.print(address, HEX);
  Serial.print(F("..."));

  Wire.beginTransmission(address);
  if (Wire.endTransmission() == 0)
  {
    mcp->begin_I2C(address);
    for (uint8_t pin = 0; pin < MCP_PIN_COUNT; pin++)
    {
      mcp->pinMode(pin, pinMode);
    }

    Serial.println(F("MCP23017"));
    return true;
  }
  else
  {
    Serial.println(F("empty"));
    return false;
  }
}
