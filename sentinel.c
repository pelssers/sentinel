// Software for the Particle Electron
// "sentinel" in the SUXESs lab.
// Documentation on Particle: docs.particle.io
//
// This software enables the Electron to:
// -Check if it is running on external power or on battery
// -Check if UPS power is on/available
// -Read detector pressure (if UPS on)
// -Automatically send alarm messages if the external power fails.
//
// The API variables:
// - "power" (integer) 0 or 1, the external power state.
// - "upspower" (integer) 0 or 1, the ups power state.
// - "pressure" (double), the pressure in mbar.
//
// The API functions:
// - "alarm", takes a string "arm" or "disarm" to enable/disable
//            the sending of messages. returns (int)1 if armed,
//            (int)0 is disarmed, (int)-1 if wrong argument.
// - "led", takes a string "on" of "off" to turn on/off an LED, for
//          future implementation of relais to switch LN2 cooling.
//
// The API events:
// - "external_power", event is published every 2m if external power
//                     is off or the external power state changes.
//                     UPS power state and pressure are also reported.
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

// Internal variables
bool is_armed;
bool prev_alarm_state;
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

void setup() {  // Mandatory function, runs once when powering up
    // Set pins
    pinMode(led, OUTPUT);
    pinMode(pressure_gauge, INPUT);
    pinMode(ups_power, INPUT);

    // Declare Particle variables, name max 12 char
    Particle.variable("power", &has_power, INT);
    Particle.variable("upspower", &has_ups_power, INT);
    Particle.variable("pressure", &pressure, DOUBLE);

    // Declare Particle functions
    Particle.function("led", ledToggle);
    Particle.function("alarm", alarmToggle);

    // Set initial power state
    has_power = hasPower();
    has_ups_power = hasUPSPower();

    // Armed by default
    is_armed = true;
    alarm_state = false;

    // Starting time
    last_publish = 0;
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

    // Check alarm conditions
    // Store previous alarm state
    prev_alarm_state = alarm_state;
    // Set current alarm state
    alarm_state = !(has_power && has_ups_power && (pressure < pressure_alarm_threshold));
    // Send alarm if the alarm state changed OR
    // the alarm state is true AND its been delta_t ms since last publish
    send_alarm = ((prev_alarm_state != alarm_state) || ((now - last_publish) >= delta_t && alarm_state));
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
