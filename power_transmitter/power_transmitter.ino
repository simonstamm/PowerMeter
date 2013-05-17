/**
 * ---------------------------------------------------------------
 *  PowerTransmitter with a JeeNode v6
 * ---------------------------------------------------------------
 * 
 * Calculating the power by measure the time elapsed between rounds from an electromechanical induction watt-hour meter
 * (in specific a metal disc / turning wheel which is made to rotate at a speed proportional to the power passing through the meter).
 *
 * It checks the metal disc / turning wheel (in German: Ferrarisscheibe) with an reflective optical sensor
 * with transistor output (CNY70) and transmits the data to a receiver.
 *
 * ---------------------------------------------------------------
 * Recommended node ID allocation
 * ---------------------------------------------------------------
 * ID		Node Type 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * 0 		Special allocation in JeeLib RFM12 driver - reserved for OOK use
 * 1-4 		Control nodes 
 * 5-10 	Energy monitoring nodes
 * 11-14 	Un-assigned
 * 15-16 	Base Station & logging nodes
 * 17-30 	Environmental sensing nodes (temperature humidity etc.)
 * 31 		Special allocation in JeeLib RFM12 driver - Node31 can communicate with nodes on any network group
 * ---------------------------------------------------------------
 */

/**
 * General Settings
 */

// Output some debugging informations to serial
boolean SERIAL_DEBUG = true;
// Transmit data over RFM12B
boolean RF12_TRANSMIT = true;
// Minimum time diff in ms for sending
const int TRANSMIT_RATE = 5000;

/**
 * Includes
 */

#include <RF12.h>

//#include "hausmessung.h"

/**
 * JeeNode Settings
 */

// Frequency of RF12B module can be RF12_433MHZ, RF12_868MHZ or RF12_915MHZ
#define freq RF12_868MHZ
// RFM12B node ID
const int nodeID = 5;
// RFM12B wireless network group - needs to be same as receiver
const int networkGroup = 210;
// Baud rate (serial)
const int baudRate = 9600;

/**
 * Thresholds
 */

// Red part of the turning wheel
#define THRESHOLD_RED_MARKER 400
// Metal part of the turning wheel
#define THRESHOLD_NORMAL 380
// Door from electric meter is open (disable detecting)
#define THRESHOLD_DISABLE 700
// Discard power values over given threshold (e.g. only accept values below 15 kW)
#define THRESHOLD_DISCARD_WATT 15000

/**
 * Transmitting packet structure
 */

typedef struct {
	byte node_id;
	int watt;
	int count;
	int tx_count;
} Payload;

// Array alias
Payload packet;

/**
 * Ports
 */

// Transistor output
const int read_pin = A2;
// Infrared emitter
const int ir_pin = 6;

/**
 * Declare variables
 */

// Node ID
byte config_node_id;

// Number of pulse
unsigned long counter;
// How much packets were sent
unsigned long tx_counter;
// Time of last transmit
unsigned long last_transmit;

// Calculated power in watt
unsigned long watt;
// Red part is visible
int in_pulse;
// Time of last pulse
unsigned long last_pulse;

// Value of the analog input (CNY70)
int read_val;
// Will store current time
unsigned long time;
// Diff between two impulses
unsigned long timediff;
// Force send of data
int send_now = 0;
// Is RF12 ready for sending
int can_send;
// RF12 header
byte header;


void setup()
{
	Serial.begin(baudRate);
	Serial.println("####################");
	Serial.println("# PowerTransmitter #");
	Serial.println("####################");

	if (RF12_TRANSMIT) {
		// Configure RF12B module
		rf12_initialize(nodeID, freq, networkGroup);

		Serial.print("[RF12]");
		config_node_id = rf12_config();
		Serial.print("[RF12] Node ID: ");
		Serial.println(config_node_id);
		Serial.println();
	}

	// Activate IR from CNY70
	pinMode(ir_pin, OUTPUT);
	digitalWrite(ir_pin, HIGH);
}


void loop()
{
	// Initialize variables
	send_now = 0;

	// Read analog input and calculate average
	read_val = 0;
	for (int i = 0; i < 10; i++) {
		read_val += analogRead(read_pin);
	}
	read_val = read_val / 10;

	if (SERIAL_DEBUG) {
		Serial.print("[>] Sensor value: ");
		Serial.print(read_val);
		Serial.println();
	}

	// Red marker must be visible
	if (read_val >= THRESHOLD_RED_MARKER && in_pulse == 0) {
		time = millis();
		timediff = time - last_pulse;

		if (SERIAL_DEBUG) {
			Serial.print("[+] Red marker detected at ");
			Serial.print(read_val);
			Serial.print(", last was ");
			Serial.print(timediff / 1000);
			Serial.print(" seconds ago. ");
			Serial.println();
		}

		// There must be at least one pulse in history to calculate the power
		if (last_pulse > 0 && timediff > 1000) {

			// 48000000 = 1000 Wh / 75 turns per KWh * 3600 (1h) * 1000 (millis)
			watt = int(48000000 / timediff);

			// Only accept values below given threshold
			if ( watt < THRESHOLD_DISCARD_WATT ) {
				if (SERIAL_DEBUG) {
					Serial.print("[+] Calculated power consumption: ");
					Serial.print(watt);
					Serial.println(" Watt");
				}

				// Remember current time
				last_pulse = time;
				in_pulse   = 1;
				send_now   = 1;
				counter++;
			} else {
				// Value is over threshold
				Serial.print("[!] Silly power value (");
				Serial.print(watt);
				Serial.print(" watt) discared.");
				Serial.println();
				watt = 0;
			}
		}

		// On the first run or on time overflow: Only store current time, but don't send any data
		if ( last_pulse == 0 || last_pulse > time ) {
			if (SERIAL_DEBUG) {
				Serial.println("[+] First round, skip calculating. Just store time.");
			}

			last_pulse = time;
			in_pulse   = 1;
			watt       = 0;
		}

	}

	// Red part of the turning wheel is gone
	if (read_val < THRESHOLD_NORMAL) {
		if (in_pulse) {
			if (SERIAL_DEBUG) {
				Serial.print("[+] Red marker is gone, metal disc returned at ");
				Serial.print(read_val);
				Serial.println(".");
			}
			in_pulse = 0;
		}
	}

	if (RF12_TRANSMIT) {
		// Should be called often, to keep receptions and transmissions going
		rf12_recvDone();
	}

	if (RF12_TRANSMIT) {
		// If it returns true, we can start the transmission
		can_send = rf12_canSend();
	} else {
		can_send = 1;
	}

	time = millis();
	timediff = time - last_transmit;

	// Checks, if we can send some data
	if ((watt > 0) && (send_now == 1 || (timediff >= TRANSMIT_RATE)) && can_send) {

		// Increment packet counter
		tx_counter++;

		// Define payload
		packet.node_id  = config_node_id;
		packet.watt     = watt;
		packet.count    = counter;
		packet.tx_count = tx_counter;

		if (SERIAL_DEBUG) {
			Serial.print("[+] Sending value #");
			Serial.print(packet.count);
			Serial.print(" (packet #");
			Serial.print(packet.tx_count);
			Serial.println(")");
		}

		if (RF12_TRANSMIT) {		
			// Send to node 1
			header = RF12_HDR_DST | 1;
			
			// Ask for ack, send to node 1
			//header = RF12_HDR_ACK | RF12_HDR_DST | 1;

			// Send power (in watt) to the receiver
			rf12_sendStart(header, &packet, sizeof packet);
		}
		
		last_transmit = millis();
		send_now = 0;
	}

}

