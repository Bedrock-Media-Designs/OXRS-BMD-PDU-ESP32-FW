/**
  ESP32 power distribution unit firmware for the Open eXtensible Rack System

  Documentation:  
    https://oxrs.io/docs/firmware/pdu-esp32.html

  Supported hardware:
    https://bmdesigns.com.au/

  GitHub repository:
    https://github.com/Bedrock-Media-Designs/OXRS-BMD-PDU-ESP32-FW

  Copyright 2019-2022 Bedrock Media Designs Ltd
*/

/*--------------------------- Libraries -------------------------------*/
#include <Adafruit_MCP23X17.h>        // For MCP23017 I/O buffers
#include <Adafruit_INA260.h>          // For INA260 current sensors
#include <OXRS_Rack32.h>              // Rack32 support
#include <OXRS_Input.h>               // For input handling
#include <OXRS_Output.h>              // For output handling
#include <OXRS_Fan.h>                 // For fan control
#include "logo.h"                     // Embedded maker logo
#include "H_Bar.h"

/*--------------------------- Constants -------------------------------*/
// Serial
#define       SERIAL_BAUD_RATE        115200

// INA260 setup (should we make this configurable?)
const INA260_AveragingCount DEFAULT_AVERAGING_COUNT = INA260_COUNT_16;
const INA260_ConversionTime DEFAULT_CONVERSION_TIME = INA260_TIME_1_1_ms;

// Can have up to 16x INA260s on a single I2C bus
const byte    INA_I2C_ADDRESS[]     = { 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F };
const uint8_t INA_COUNT             = sizeof(INA_I2C_ADDRESS);

// Define the MCP addresses
const byte    MCP_I2C_ADDRESS[]     = { 0x20, 0x21 };
const uint8_t MCP_COUNT             = sizeof(MCP_I2C_ADDRESS);

#define       MCP_OUTPUT_INDEX        0
#define       MCP_INPUT_INDEX         1

// Each MCP23017 has 16 I/O pins
#define       MCP_PIN_COUNT           16

// Speed up the I2C bus to get faster event handling
#define       I2C_CLOCK_SPEED         400000L

// Can only display a max of 8 horizontal bars (plus a "T"otal bar)
#define       MAX_HBAR_COUNT          8

// Default maximum mA for each output (configurable via "overCurrentLimitMilliAmps")
#define       DEFAULT_OVERCURRENT_MA  2000L

// Cycle time to read INAs (INA260_TIME_x * INA260_COUNT_x * 2 + margin)
// set to 40ms (25Hz scan frequency)
#define       INA_CYCLE_TIME          40L

// Alert types
#define       ALERT_TYPE_NONE         0
#define       ALERT_TYPE_V_OVER       1
#define       ALERT_TYPE_V_UNDER      2
#define       ALERT_TYPE_I_OVER       3
#define       ALERT_TYPE_I_OVER_TOTAL 4

/*--------------------------- Global Variables ------------------------*/
// Each bit corresponds to a device found on the IC2 bus
uint8_t g_inasFound = 0;
uint8_t g_mcpsFound = 0;

// Publish telemetry data interval - extend or disable via the config
// option "publishPduTelemetrySeconds" - default to 60s, zero to disable
uint32_t g_publishTelemetry_ms      = 60000L;
uint32_t g_lastPublishTelemetry     = 0L;

// Supply voltage is limited to 12V only - we set limits at +/-2V
uint32_t g_supplyVoltage_mV         = 12000L;
uint32_t g_supplyVoltageDelta_mV    = 2000L;

// Current limit is configurable for combined and individual outputs
uint32_t g_overCurrentLimit_mA      = 10000L;

// Timer for INA scan cycle timing
uint32_t g_inaTimer                 = 0L;

/*--------------------------- Instantiate Globals ---------------------*/
// Rack32 handler
OXRS_Rack32 rack32(FW_LOGO);

// Current sensors
Adafruit_INA260 ina260[INA_COUNT];

// I/O buffers
Adafruit_MCP23X17 mcp23017[MCP_COUNT];

// I/O handlers
OXRS_Output oxrsOutput;
OXRS_Input oxrsInput;

// Fan control
OXRS_Fan oxrsFan;

// Horizontal display bars (only display MAX_HBAR_COUNT + total bar)
H_Bar hBar[INA_COUNT + 1];

// TODO: need an internal datatype to store per-port limits and alert timers
//       so we can detect when we alert and not start shutting things down
//       till a grace period has elapsed


/*--------------------------- Program ---------------------------------*/
void setBarValue(int ina, float mA, float mV)
{
  hBar[ina].setValue(mA, mV);
}

void setBarState(int ina, int state)
{
  hBar[ina].setState(state);
}

void setBarMaxValue(int ina, float mA)
{
  hBar[ina].setMaxValue(mA);
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

void getAlertEventType(char eventType[], uint8_t alertType)
{
  // Determine what alert type we need to publish
  sprintf_P(eventType, PSTR("error"));
  switch (alertType)
  {
    case ALERT_TYPE_NONE:
      sprintf_P(eventType, PSTR("none"));
      break;
    case ALERT_TYPE_V_OVER:
      sprintf_P(eventType, PSTR("overVoltage"));
      break;
    case ALERT_TYPE_V_UNDER:
      sprintf_P(eventType, PSTR("underVoltage"));
      break;
    case ALERT_TYPE_I_OVER:
      sprintf_P(eventType, PSTR("overCurrent"));
      break;
    case ALERT_TYPE_I_OVER_TOTAL:
      sprintf_P(eventType, PSTR("overCurrentTotal"));
      break;
  }
}

int checkVoltageLimits(float mV)
{
  uint32_t underLimit_mV = g_supplyVoltage_mV - g_supplyVoltageDelta_mV;
  uint32_t overLimit_mV = g_supplyVoltage_mV + g_supplyVoltageDelta_mV;

  if (mV < underLimit_mV) { return -1; }
  if (mV > overLimit_mV)  { return 1; }

  return 0;
}

void publishTelemetry(float mA[], float mV[], float mW[])
{
  // Ignore if publishing has been disabled
  if (g_publishTelemetry_ms == 0) { return; }

  // Check if we are ready to publish
  if ((millis() - g_lastPublishTelemetry) > g_publishTelemetry_ms)
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
    }

    // Publish to MQTT
    if (telemetry.size() > 0)
    {
      rack32.publishTelemetry(telemetry.as<JsonVariant>());
    }
    
    // Reset our timer
    g_lastPublishTelemetry = millis();
  }
}

uint8_t getIndex(JsonVariant json)
{
  if (!json.containsKey("index"))
  {
    rack32.println(F("[pdu ] missing index"));
    return 0;
  }
  
  uint8_t index = json["index"].as<uint8_t>();
  
  // Check the index is valid for this device
  if (index <= 0 || index > INA_COUNT)
  {
    rack32.println(F("[pdu ] invalid index"));
    return 0;
  }

  // Check the index corresponds to an existing INA260 (index is 1-based)
  if (bitRead(g_inasFound, index - 1) == 0)
  {
    rack32.println(F("[pdu ] invalid index, no INA260 found"));
    return 0;
  }
  
  return index;
}

void publishOutputEvent(uint8_t index, uint8_t type, uint8_t state)
{
  char outputType[16];
  getOutputType(outputType, type);
  char outputEvent[16];
  getOutputEventType(outputEvent, state);

  StaticJsonDocument<128> json;
  json["index"] = index;
  json["type"] = outputType;
  json["event"] = outputEvent;

  if (!rack32.publishStatus(json.as<JsonVariant>()))
  {
    rack32.print(F("[pdu ] [failover] "));
    serializeJson(json, rack32);
    rack32.println();

    // TODO: add failover handling code here
  }
}

void publishAlertEvent(uint8_t index, uint8_t alertType)
{
  char alertEvent[32];
  getAlertEventType(alertEvent, alertType);

  StaticJsonDocument<128> json;
  json["index"] = index;
  json["type"] = "alert";
  json["event"] = alertEvent;

  if (!rack32.publishStatus(json.as<JsonVariant>()))
  {
    rack32.print(F("[pdu ] [failover] "));
    serializeJson(json, rack32);
    rack32.println();

    // TODO: add failover handling code here
  }
}

/**
  Config handler
 */
void outputConfigSchema(JsonVariant json)
{
  JsonObject outputs = json.createNestedObject("outputs");
  outputs["title"] = "Output Configuration";
  outputs["description"] = "Add configuration for each output on your device. The 1-based index specifies which output you wish to configure. An output will shutdown if the reading from the current sensor exceeds the over current limit (defaults to 2000mA or 2A, must be a number between 1 and 5000).";
  outputs["type"] = "array";
  
  JsonObject items = outputs.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject index = properties.createNestedObject("index");
  index["title"] = "Index";
  index["type"] = "integer";
  index["minimum"] = 1;
  index["maximum"] = INA_COUNT;

  JsonObject overCurrentLimitMilliAmps = properties.createNestedObject("overCurrentLimitMilliAmps");
  overCurrentLimitMilliAmps["title"] = "Over Current Limit (mA)";
  overCurrentLimitMilliAmps["type"] = "integer";
  overCurrentLimitMilliAmps["minimum"] = 1;
  overCurrentLimitMilliAmps["maximum"] = 5000;

  JsonArray required = items.createNestedArray("required");
  required.add("index");
}

void setConfigSchema()
{
  // Define our config schema
  DynamicJsonDocument json(4096);
  JsonVariant config = json.as<JsonVariant>();

  JsonObject publishPduTelemetrySeconds = config.createNestedObject("publishPduTelemetrySeconds");
  publishPduTelemetrySeconds["title"] = "Publish PDU Telemetry (seconds)";
  publishPduTelemetrySeconds["description"] = "How often to publish telemetry data from the onboard INA260 current sensors (defaults to 60 seconds, setting to 0 disables telemetry reports). Must be a number between 0 and 86400 (i.e. 1 day).";
  publishPduTelemetrySeconds["type"] = "integer";
  publishPduTelemetrySeconds["minimum"] = 0;
  publishPduTelemetrySeconds["maximum"] = 86400;

  JsonObject overCurrentLimitMilliAmps = config.createNestedObject("overCurrentLimitMilliAmps");
  overCurrentLimitMilliAmps["title"] = "Over Current Limit (mA)";
  overCurrentLimitMilliAmps["description"] = "If the readings from all current sensors add up to more than this limit then shutdown all outputs (defaults to 10000mA or 10A). Must be a number between 1 and 15000 (i.e. 15A).";
  overCurrentLimitMilliAmps["type"] = "integer";
  overCurrentLimitMilliAmps["minimum"] = 1;
  overCurrentLimitMilliAmps["maximum"] = 15000;

  outputConfigSchema(config);

  // Add any fan control config
  oxrsFan.setConfigSchema(config);

  // Pass our config schema down to the Rack32 library
  rack32.setConfigSchema(config);
}

void jsonOutputConfig(JsonVariant json)
{
  uint8_t index = getIndex(json);
  if (index == 0) return;

  // Index is 1-based
  uint8_t ina = index - 1;
  
  if (json.containsKey("overCurrentLimitMilliAmps"))
  {
    uint32_t overCurrentLimit_mA = json["overCurrentLimitMilliAmps"].as<uint32_t>();

    // Set the alert limit on the INA260 and re-scale the bar graph on the display
    ina260[ina].setAlertLimit(overCurrentLimit_mA);
    setBarMaxValue(ina, overCurrentLimit_mA);
  }
}

void jsonConfig(JsonVariant json)
{
  if (json.containsKey("publishPduTelemetrySeconds"))
  {
    g_publishTelemetry_ms = json["publishPduTelemetrySeconds"].as<uint32_t>() * 1000L;
  }

  if (json.containsKey("overCurrentLimitMilliAmps"))
  {
    g_overCurrentLimit_mA = json["overCurrentLimitMilliAmps"].as<uint32_t>();
    setBarMaxValue(INA_COUNT, g_overCurrentLimit_mA);
  }

  if (json.containsKey("outputs"))
  {
    for (JsonVariant output : json["outputs"].as<JsonArray>())
    {
      jsonOutputConfig(output);
    }
  }

  // Pass on to the fan control library
  oxrsFan.onConfig(json);
}

/**
  Command handler
 */
void outputCommandSchema(JsonVariant json)
{
  JsonObject outputs = json.createNestedObject("outputs");
  outputs["title"] = "Output Commands";
  outputs["description"] = "Send commands to one or more outputs on your device. The 1-based index specifies which output you wish to command. Supported commands are ‘on’ or ‘off’ to change the output state, or ‘query’ to publish the current state to MQTT.";
  outputs["type"] = "array";
  
  JsonObject items = outputs.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject index = properties.createNestedObject("index");
  index["title"] = "Index";
  index["type"] = "integer";
  index["minimum"] = 1;
  index["maximum"] = INA_COUNT;

  JsonObject command = properties.createNestedObject("command");
  command["title"] = "Command";
  command["type"] = "string";
  JsonArray commandEnum = command.createNestedArray("enum");
  commandEnum.add("query");
  commandEnum.add("on");
  commandEnum.add("off");

  JsonArray required = items.createNestedArray("required");
  required.add("index");
  required.add("command");
}

void setCommandSchema()
{
  // Define our config schema
  DynamicJsonDocument json(4096);
  JsonVariant command = json.as<JsonVariant>();

  outputCommandSchema(command);
  
  // Add any fan control commands
  oxrsFan.setCommandSchema(command);

  // Pass our command schema down to the Rack32 library
  rack32.setCommandSchema(command);
}

void jsonOutputCommand(JsonVariant json)
{
  uint8_t index = getIndex(json);
  if (index == 0) return;

  // Index is 1-based
  uint8_t pin = index - 1;
  
  if (json.containsKey("command"))
  {
    if (json["command"].isNull() || strcmp(json["command"], "query") == 0)
    {
      // Publish a status event with the current state
      // NOTE: the PDU relays are NC - so LOW is on, HIGH is off
      uint8_t state = mcp23017[MCP_OUTPUT_INDEX].digitalRead(pin) == LOW ? RELAY_ON : RELAY_OFF;
      publishOutputEvent(index, RELAY, state);
    }
    else
    {
      // Send this command down to our output handler to process
      if (strcmp(json["command"], "on") == 0)
      {
        oxrsOutput.handleCommand(MCP_OUTPUT_INDEX, pin, RELAY_ON);
      }
      else if (strcmp(json["command"], "off") == 0)
      {
        oxrsOutput.handleCommand(MCP_OUTPUT_INDEX, pin, RELAY_OFF);
      }
      else 
      {
        rack32.println(F("[pdu ] invalid command"));
      }
    }
  }
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

  // Pass on to the fan control library
  oxrsFan.onCommand(json);
}

/**
  Event handlers
*/
void outputEvent(uint8_t id, uint8_t output, uint8_t type, uint8_t state)
{
  // Update the MCP pin - i.e. turn the relay on/off
  // NOTE: the PDU relays are NC - so LOW to turn on, HIGH to turn off
  mcp23017[id].digitalWrite(output, state == RELAY_ON ? LOW : HIGH);

  // Update the display
  setBarState(output, state == RELAY_ON ? STATE_ON : STATE_OFF);

  // Publish the event (index is 1-based)
  publishOutputEvent(output + 1, type, state);
}

void inputEvent(uint8_t id, uint8_t input, uint8_t type, uint8_t state)
{
  // Check the input corresponds to an existing INA260 (we always read all 16 pins on
  // the input MCP so just ignore any events for those without a corresponding output)
  if (bitRead(g_inasFound, input) == 0)
    return;

  // Pass this event straight thru to the output handler, using same index
  uint8_t outputType = RELAY;
  uint8_t outputState = state == LOW_EVENT ? RELAY_ON : RELAY_OFF;

  outputEvent(MCP_OUTPUT_INDEX, input, outputType, outputState);
}

void processInas()
{
  if ((millis() - g_inaTimer) > INA_CYCLE_TIME)
  {
    g_inaTimer = millis();
    
    float mA[INA_COUNT];
    float mV[INA_COUNT];
    float mW[INA_COUNT];
    uint8_t alertType[INA_COUNT];

    float mATotal = 0;

    // Iterate through each of the INA260s found on the I2C bus
    for (uint8_t ina = 0; ina < INA_COUNT; ina++)
    {
      if (bitRead(g_inasFound, ina) == 0)
        continue;

      // Read the values for this sensor
      mA[ina] = ina260[ina].readCurrent();
      mV[ina] = ina260[ina].readBusVoltage();
      mW[ina] = ina260[ina].readPower();

      // We are using the internal over-current alert type
      alertType[ina] = ina260[ina].alertFunctionFlag() ? ALERT_TYPE_I_OVER : ALERT_TYPE_NONE;

      // Keep track of total current
      mATotal += mA[ina];
    }

    // Check for any alerted outputs and shut them off
    for (uint8_t ina = 0; ina < INA_COUNT; ina++)
    {
      if (bitRead(g_inasFound, ina) == 0)
        continue;

      // Check for any manual alert states if not already alerted
      if (alertType[ina] == ALERT_TYPE_NONE)
      {
        // Check bus voltage limits and set manual alert states
        int voltageCheck = checkVoltageLimits(mV[ina]);
        if (voltageCheck < 0)
        {
          // Under-voltage alert
          alertType[ina] = ALERT_TYPE_V_UNDER;
        }
        else if (voltageCheck > 0)
        {
          // Over-voltage alert
          alertType[ina] = ALERT_TYPE_V_OVER;
        }      
        else if (mATotal >= g_overCurrentLimit_mA)
        {
          // Total over-current alert
          alertType[ina] = ALERT_TYPE_I_OVER_TOTAL;
        }
      }

      // Shut off any output in an alerted state
      if (alertType[ina] != ALERT_TYPE_NONE)
      {
        // Turn off relay if it is currently on
        // NOTE: the PDU relays are NC - so LOW to turn on, HIGH to turn off
        if (LOW == mcp23017[MCP_OUTPUT_INDEX].digitalRead(ina))
        {
          outputEvent(MCP_OUTPUT_INDEX, ina, RELAY, RELAY_OFF);
        }

        // Publish an alert event
        publishAlertEvent(ina, alertType[ina]);

        // Update the bar state to ALERT
        setBarState(ina, STATE_ALERT);
      }

      // Update the display for this sensor
      setBarValue(ina, mA[ina], mV[ina]);
    }    

    // Update the display total
    setBarValue(INA_COUNT, mATotal, NAN);

    // Publish telemetry data if required
    publishTelemetry(mA, mV, mW);
  }
}

void processMcps()
{
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
    
    // Check for any input events
    if (mcp == MCP_INPUT_INDEX)
    {
      oxrsInput.process(mcp, mcp23017[mcp].readGPIOAB());    
    }
  }
}

void processFans()
{
  // Let fan controllers handle any events etc
  oxrsFan.loop();

  // Publish fan telemetry
  DynamicJsonDocument telemetry(4096);
  oxrsFan.getTelemetry(telemetry.as<JsonVariant>());
  
  if (telemetry.size() > 0)
  {
    rack32.publishTelemetry(telemetry.as<JsonVariant>());
  }
}

/**
  I2C
 */
void scanI2CBus()
{
  // Initialise current sensors
  rack32.println(F("[pdu ] scanning for current sensors..."));

  for (uint8_t ina = 0; ina < INA_COUNT; ina++)
  {
    rack32.print(F(" - 0x"));
    rack32.print(INA_I2C_ADDRESS[ina], HEX);
    rack32.print(F("..."));

    if (ina260[ina].begin(INA_I2C_ADDRESS[ina]))
    {
      bitWrite(g_inasFound, ina, 1);
      rack32.println(F("INA260"));

      // Set the number of samples to average
      ina260[ina].setAveragingCount(DEFAULT_AVERAGING_COUNT);
      
      // Set the time over which to measure the current and bus voltage
      ina260[ina].setVoltageConversionTime(DEFAULT_CONVERSION_TIME);
      ina260[ina].setCurrentConversionTime(DEFAULT_CONVERSION_TIME);

      // Set the polarity and disable latching so the alert resets
      ina260[ina].setAlertPolarity(INA260_ALERT_POLARITY_NORMAL);
      ina260[ina].setAlertLatch(INA260_ALERT_LATCH_TRANSPARENT);

      // Default the over current alert at 2000mA (2A)
      ina260[ina].setAlertType(INA260_ALERT_OVERCURRENT);
      ina260[ina].setAlertLimit(DEFAULT_OVERCURRENT_MA);
    }
    else
    {
      rack32.println(F("empty"));
    }
  }

  // Initialise I/O buffers
  rack32.println(F("[pdu ] scanning for I/O buffers..."));

  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    rack32.print(F(" - 0x"));
    rack32.print(MCP_I2C_ADDRESS[mcp], HEX);
    rack32.print(F("..."));
  
    Wire.beginTransmission(MCP_I2C_ADDRESS[mcp]);
    if (Wire.endTransmission() == 0)
    {
      bitWrite(g_mcpsFound, mcp, 1);
      rack32.println(F("MCP23017"));
  
      mcp23017[mcp].begin_I2C(MCP_I2C_ADDRESS[mcp]);
      for (uint8_t pin = 0; pin < MCP_PIN_COUNT; pin++)
      {
        mcp23017[mcp].pinMode(pin, mcp == MCP_OUTPUT_INDEX ? OUTPUT : INPUT_PULLUP);
      }
  
      if (mcp == MCP_OUTPUT_INDEX)
      {
        // Initialise the output handler (default to RELAY, not configurable)
        // NOTE: the PDU relays are NC - so startup in ON state
        oxrsOutput.begin(outputEvent, RELAY, RELAY_ON);
      }
      if (mcp == MCP_INPUT_INDEX)
      {
        // Initialise the input handler (default to SWITCH, not configurable)
        oxrsInput.begin(inputEvent, SWITCH);
      }
    }
    else
    {
      rack32.println(F("empty"));
    }
  }
}

/**
  LCD
 */
void initialiseScreen()
{
  OXRS_LCD* screen = rack32.getLCD();
  
  // Override standard screen config - hide MAC address
  screen->setIPpos(45);
  screen->setMACpos(0);
  screen->setMQTTpos(60);
  screen->setTEMPpos(75);

  // Initialise a bar for the first MAX_HBAR_COUNT sensors found
  uint8_t barCount = 0;
  uint8_t y = 95;
  for (uint8_t ina = 0; ina < INA_COUNT; ina++)
  {
    if (bitRead(g_inasFound, ina) == 0)
      continue;

    if (barCount++ >= MAX_HBAR_COUNT)
      continue;

    // Display index is 1-based
    hBar[ina].begin(screen->getTft(), y, ina + 1);    
    hBar[ina].setMaxValue(DEFAULT_OVERCURRENT_MA);
    y += 14;
  }
  
  // Initialise a bar for the "T"otal
  hBar[INA_COUNT].begin(screen->getTft(), y, 0);
  hBar[INA_COUNT].setMaxValue(g_overCurrentLimit_mA);
}

/**
  Setup
*/
void setup()
{
  // Start serial and let settle
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  Serial.println(F("[pdu ] starting up..."));

  // Start the I2C bus
  Wire.begin();

  // Scan the I2C bus and set up current sensors and I/O buffers
  scanI2CBus();

  // Scan for and initialise any fan controllers found on the I2C bus
  oxrsFan.begin();

  // Start Rack32 hardware
  rack32.begin(jsonConfig, jsonCommand);

  // Setup custom LCD
  initialiseScreen();
  
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

  // Process INA260 sensors
  processInas();

  // Process MCPs
  processMcps();

  // Process fans
  processFans();
}