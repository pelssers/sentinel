// Software for the Particle Electron
// "sentinel" in the SUXESs lab.
// Documentation on Particle: docs.particle.io
//
// This software enables the Electron to:
// -Check if it is running on external power or on battery
// -Check if UPS power is on/available
// -Read detector pressure (if UPS on)
// -Automatically send alarm messages if either the
//  external power or ups power fails. Or if the pressure
//  exceeds a certain threshold (default 2500 mbar).
//
// The API variables:
// - "power" (integer) 0 or 1, the external power state.
// - "upspower" (integer) 0 or 1, the ups power state.
// - "pressure" (double), the pressure in mbar.
// - "status" (string), status message.
//
// The API functions:
// - "alarm", takes a string "arm" or "disarm" to enable/disable
//            the sending of messages. returns (int)1 if armed,
//            (int)0 if disarmed, (int)-1 if wrong argument.
// - "led", takes a string "on" of "off" to turn on/off an LED, for
//          future implementation of relais to switch LN2 cooling.
// - "test", no arguments, publishes test event.
// - "threshold", takes a string to update the pressure alarm
//                threshold. Returns (int)0 if conversion fails,
//                returns (int)new_threshold otherwise.
//                Example: "2500.0" to set 2500 mbar threshold.
//
// The API events:
// - "external_power", event is published every 2m if in alarm state
//                     Power, UPS power and pressure are reported.
//                     "Power OK/DOWN, UPS OK/DOWN, Pressure %.2f mbar".
//
// Bart Pelssers 2018

// Access the Power Management IC
PMIC pmic;

// Label the board connections
int led = D6;  // Digital pin 6
int pressure_gauge = A0;  // Analog pin 0
int ups_power = A3;  // Analog pin 3

// Variables to publish
int has_power;  // no bool because of Particle.variable
int has_ups_power;  // no bool because of Particle.variable
double pressure;
String status;

// Internal variables
bool is_armed;
bool alarm_state;
bool send_alarm;
unsigned long now;
unsigned long last_publish;
unsigned long delta_t = 120000;  // Publish interval if power down [ms]
double pressure_alarm_threshold = 2500.;  // Send alarm if above threshold in [mbar]

int hasPower() {
    // Check if the unit has an external power source,
    // either through USB or the VIN power pin.
    // Read systemStatus byte, bit 2 (mask 0x4) encodes
    // power state PG_STAT. PG_STAT > 0 means powered.
    byte systemStatus = pmic.getSystemStatus();
    return int((systemStatus & 0x04) != 0);
}

int hasUPSPower() {
    // Check if UPS power is available
    // Measured on 'ups_power' pin A3
    // Power should be around 2.6 V
    // Digitized by 12-bit digitizer (4096 channels).
    // Set 1.3V threshold, 4096 * 1.3 / 3.3 = 1614
    int counts = analogRead(ups_power);
    if (counts > 1614) {
        return 1;
    } else {
        return 0;
    }
}

double readPressure() {
    // Read the pressure gauge
    // Set up for PG inner detector
    // gauge output 0-10VDC, voltage divided to 3.3VDC
    // Digitized by 12-bit ditizer (4096 channels)
    // pressure [mbar] = 626.3 * (adc_counts / 4096) * 10 - 631.4
    int adc_counts = analogRead(pressure_gauge);
    return 6263. * adc_counts / 4096. - 631.4;
}

int ledToggle(String command) {
    // Switch the led on or off
    // Led not used, maybe keep for future LN2 switch
    if (command == "on") {
        digitalWrite(led, HIGH);
        return 1;
    } else if (command == "off") {
        digitalWrite(led, LOW);
        return 0;
    } else {
        return -1;
    }
}

int alarmToggle(String command) {
    // Arm/disarm the alarms
    if (command == "arm") {
        is_armed = true;
        return 1;
    } else if (command == "disarm") {
        is_armed = false;
        return 0;
    } else {
        return -1;
    }
}

int setPThresh(String command) {
    // Set pressure threshold
    // Attempt float conversion
    double new_threshold = command.toFloat();
    if (new_threshold == 0) {  // No valid conversion
        return 0;
    } else {
        // Set new threshold
        pressure_alarm_threshold = new_threshold;
        // Return current threshold (as int)
        return (int) pressure_alarm_threshold;
    }
}

int testEvent(String command) {
    // Publish test event
    String message = "TEST: This is a test event";

    Particle.publish("external_power",  // event name
                     message,           // event data
                     60,                // event TTL [s]
                     PRIVATE);          // event scope
    return 1;
}

void setup() {  // Mandatory function, runs once when powering up
    // Set pins
    pinMode(led, OUTPUT);
    pinMode(pressure_gauge, INPUT);
    pinMode(ups_power, INPUT);

    // Declare Particle variables, name max 12 char
    Particle.variable("power", has_power);
    Particle.variable("upspower", has_ups_power);
    Particle.variable("pressure", pressure);
    Particle.variable("status", status);

    // Declare Particle functions
    Particle.function("led", ledToggle);
    Particle.function("alarm", alarmToggle);
    Particle.function("test", testEvent);
    Particle.function("threshold", setPThresh);

    // Set initial power state
    has_power = hasPower();
    has_ups_power = hasUPSPower();

    // Armed by default
    is_armed = true;
    alarm_state = false;

    // Starting time and status
    last_publish = 0;
    status = "setup";
}

void loop() {  // Mandatory function, loops forever
    // Update pressure
    pressure = readPressure();

    // Update UPS power state
    has_ups_power = hasUPSPower();

    // Update power state
    has_power = hasPower();

    // Timestamp (milliseconds since powerup)
    now = millis();

    // Update status message
    status = String::format("power:%d,ups:%d,pressure:%.2f,pthresh:%d,armed:%d",
                            has_power,
                            has_ups_power,
                            pressure,
                            (int)pressure_alarm_threshold,
                            (int)is_armed);

    // Check alarm conditions
    // Set current alarm state
    alarm_state = !(has_power && has_ups_power && (pressure < pressure_alarm_threshold));
    // Send alarm if the alarm state is true AND its been delta_t ms since last publish
    send_alarm = ((now - last_publish) > delta_t && alarm_state);
    if (is_armed && send_alarm) {
        // Publish alarm message
        String message = String::format("Power %s, UPS %s, Pressure %.2f mbar",
                                        has_power ? "OK" : "DOWN",
                                        has_ups_power ? "OK" : "DOWN",
                                        pressure);

        Particle.publish("external_power",  // event name
                         message,           // event data
                         60,                // event TTL [s]
                         PRIVATE);          // event scope
        last_publish = now;
    }
}
