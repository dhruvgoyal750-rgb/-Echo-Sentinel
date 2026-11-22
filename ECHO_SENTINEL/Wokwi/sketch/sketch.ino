#include <Arduino.h>
#include <driver/rmt.h>
#include <esp_err.h>

#define TRIG_PIN 5

#define ECHO_PIN 18

// Timing configuration

static const uint32_t TRIGGER_INTERVAL_US = 60000;

static const uint32_t TRIGGER_PULSE_US = 10;

static const uint32_t RISE_TIMEOUT_US = 30000;

static const uint32_t FALL_TIMEOUT_US = 30000;

static const uint32_t STALE_LIMIT_US = 150000;

// Distance limits

static const uint32_t MIN_DISTANCE_MM = 20;

static const uint32_t MAX_DISTANCE_MM = 4000;

// Outlier filter

static const uint32_t MAX_SLEW_MM = 500;

static const uint8_t FILTER_SIZE = 3;

// Status codes

enum SensorStatus {

SENSOR_OK,

NO_ECHO_TIMEOUT,

STUCK_HIGH,

STUCK_LOW,

OUT_OF_RANGE,

OUTLIER_REJECTED,

STALE

};

enum DriverState {

IDLE,

TRIGGERING,

WAIT_RISE,

WAIT_FALL

};

struct SensorSnapshot {

uint32_t distance_mm;

SensorStatus status;

uint32_t seq;

uint32_t t_capture_us;

};

// Echo interrupt variables

volatile uint32_t riseTimeUs = 0;

volatile uint32_t fallTimeUs = 0;

volatile bool riseDetected = false;

volatile bool fallDetected = false;

portMUX_TYPE echoMux = portMUX_INITIALIZER_UNLOCKED;

// Snapshot double buffer

SensorSnapshot snapshots[2] = {};

volatile uint8_t activeBuffer = 0;

portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;

// Driver variables

uint32_t sequenceNumber = 0;

uint32_t lastTriggerUs = 0;

uint32_t phaseStartUs = 0;

uint32_t triggerStartUs = 0;

// Hardware-generated 10 us TRIG pulse using the ESP32 RMT peripheral.
// RMT runs at 1 MHz (80 MHz APB / clock divider 80).
static const rmt_channel_t TRIG_RMT_CHANNEL = RMT_CHANNEL_0;
volatile bool triggerPulseDone = false;
rmt_item32_t triggerRmtItem = {};

static void IRAM_ATTR onTriggerRmtDone(rmt_channel_t channel, void *arg) {
  triggerPulseDone = true;
}

bool initializeTriggerRmt() {
  rmt_config_t config = RMT_DEFAULT_CONFIG_TX(
      (gpio_num_t)TRIG_PIN, TRIG_RMT_CHANNEL);
  config.clk_div = 80;
  config.mem_block_num = 1;
  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

  if (rmt_config(&config) != ESP_OK) return false;
  if (rmt_driver_install(TRIG_RMT_CHANNEL, 0, 0) != ESP_OK) return false;
  // This ESP32 Arduino/IDF version returns the previous callback pointer,
  // not an esp_err_t status code. Register the callback and ignore that
  // previous-callback return value.
  rmt_register_tx_end_callback(onTriggerRmtDone, nullptr);

  triggerRmtItem.duration0 = TRIGGER_PULSE_US;  // 10 ticks = 10 us HIGH
  triggerRmtItem.level0 = 1;
  triggerRmtItem.duration1 = 1;                 // 1 us LOW terminator
  triggerRmtItem.level1 = 0;
  return true;
}

bool triggerRmtReady = false;

uint32_t lastPrintUs = 0;

// Software fault-injection timing (H command).

// The timer starts when the serial command is accepted.

bool highFaultInjectionArmed = false;

uint32_t highFaultInjectionStartUs = 0;



// Software STUCK_LOW injection suppresses Echo-rise acceptance for

// two measurement cycles, matching the driver's two-timeout classifier.

bool lowFaultInjectionArmed = false;

bool lowFaultInjectionActive = false;

uint32_t lowFaultInjectionStartUs = 0;

uint32_t invalidCount = 0;

bool sensorFault = false;

uint8_t consecutiveLowTimeouts = 0;

DriverState driverState = IDLE;

// Filter variables

uint32_t filterValues[FILTER_SIZE] = {};

uint8_t filterCount = 0;

uint8_t filterIndex = 0;

bool hasAcceptedReading = false;

uint32_t lastAcceptedDistance = 0;



// M1.8 filter-latency instrumentation. Measures from the first

// accepted raw sample that changes by >100 mm until the median

// filtered output changes by >100 mm from its previous baseline.

bool filterLatencyTracking = false;

bool filterLatencyReported = false;

uint32_t filterLatencyStartUs = 0;

uint32_t filterLatencyBaselineMm = 0;

// ========================================

// STATUS TEXT

// ========================================

const char* statusText(SensorStatus status) {

switch (status) {

case SENSOR_OK: return "OK";

case NO_ECHO_TIMEOUT: return "NO_ECHO_TIMEOUT";

case STUCK_HIGH: return "STUCK_HIGH";

case STUCK_LOW: return "STUCK_LOW";

case OUT_OF_RANGE: return "OUT_OF_RANGE";

case OUTLIER_REJECTED: return "OUTLIER_REJECTED";

case STALE: return "STALE";

default: return "UNKNOWN";

}

}

// ========================================

// ECHO WIDTH

// ========================================

uint32_t calculateEchoWidthUs(

uint32_t riseTime,

uint32_t fallTime

) {

return (uint32_t)(fallTime - riseTime);

}

// ========================================

// ECHO INTERRUPT

// ========================================

void IRAM_ATTR echoISR() {

uint32_t now = micros();

portENTER_CRITICAL_ISR(&echoMux);

if (digitalRead(ECHO_PIN) == HIGH) {

riseTimeUs = now;

riseDetected = true;

fallDetected = false;

} else {

if (riseDetected) {

fallTimeUs = now;

fallDetected = true;

}

}

portEXIT_CRITICAL_ISR(&echoMux);

}

// ========================================

// MEDIAN FILTER

// ========================================

uint32_t medianOfThree(

uint32_t a,

uint32_t b,

uint32_t c

) {

if (a > b) {

uint32_t t = a;

a = b;

b = t;

}

if (b > c) {

uint32_t t = b;

b = c;

c = t;

}

if (a > b) {

uint32_t t = a;

a = b;

b = t;

}

return b;

}

uint32_t applyMedianFilter(uint32_t distance) {

filterValues[filterIndex] = distance;

filterIndex = (filterIndex + 1) % FILTER_SIZE;

if (filterCount < FILTER_SIZE) {

filterCount++;

}

if (filterCount == 1) {

return filterValues[0];

}

if (filterCount == 2) {

return (filterValues[0] + filterValues[1]) / 2;

}

return medianOfThree(

filterValues[0],

filterValues[1],

filterValues[2]

);

}

// ========================================

// OUTLIER DETECTION

// ========================================

bool isOutlier(uint32_t distance) {

if (!hasAcceptedReading) {

return false;

}

uint32_t difference;

if (distance > lastAcceptedDistance) {

difference = distance - lastAcceptedDistance;

} else {

difference = lastAcceptedDistance - distance;

}

return difference > MAX_SLEW_MM;

}

// ========================================

// SNAPSHOT PUBLISH

// ========================================

void publishSnapshot(

uint32_t distance,

SensorStatus status

) {

SensorSnapshot newSnapshot;

newSnapshot.distance_mm = distance;

newSnapshot.status = status;

newSnapshot.seq = ++sequenceNumber;

newSnapshot.t_capture_us = micros();

portENTER_CRITICAL(&snapshotMux);

uint8_t nextBuffer = 1 - activeBuffer;

snapshots[nextBuffer] = newSnapshot;

activeBuffer = nextBuffer;

portEXIT_CRITICAL(&snapshotMux);

if (status == SENSOR_OK) {

invalidCount = 0;

sensorFault = false;

} else {

invalidCount++;

if (invalidCount >= 3) {

sensorFault = true;

}

}

}

// ========================================

// SNAPSHOT READ

// ========================================

SensorSnapshot readSnapshot() {

SensorSnapshot copy;

portENTER_CRITICAL(&snapshotMux);

copy = snapshots[activeBuffer];

portEXIT_CRITICAL(&snapshotMux);

return copy;

}

// ========================================

// AGE ACCESSOR

// ========================================

uint32_t getMeasurementAgeUs(

const SensorSnapshot &snapshot

) {

return (uint32_t)(

micros() - snapshot.t_capture_us

);

}

// ========================================

// CURRENT SNAPSHOT

// ========================================

SensorSnapshot getCurrentSnapshot() {

SensorSnapshot current = readSnapshot();

uint32_t age = getMeasurementAgeUs(current);

if (

current.status == SENSOR_OK &&

age > STALE_LIMIT_US

) {

current.status = STALE;

}

return current;

}

// ========================================

// RESET ECHO FLAGS

// ========================================

void resetEchoFlags() {

portENTER_CRITICAL(&echoMux);

riseDetected = false;

fallDetected = false;

riseTimeUs = 0;

fallTimeUs = 0;

portEXIT_CRITICAL(&echoMux);

}

// ========================================

// READ ECHO FLAGS

// ========================================

void readEchoFlags(

bool &rise,

bool &fall,

uint32_t &riseTime,

uint32_t &fallTime

) {

portENTER_CRITICAL(&echoMux);

rise = riseDetected;

fall = fallDetected;

riseTime = riseTimeUs;

fallTime = fallTimeUs;

portEXIT_CRITICAL(&echoMux);

}

// ========================================

// SOFTWARE FAULT-INJECTION TIMING

// ========================================

void logHighInjectionTiming() {

uint32_t elapsedUs =

(uint32_t)(micros() - highFaultInjectionStartUs);

Serial.print("FAULT_INJECTION_TIMING | Status: STUCK_HIGH");

Serial.print(" | From command to detection: ");

Serial.print(elapsedUs);

Serial.println(" us");

}

void logLowInjectionTiming() {

uint32_t elapsedUs =

(uint32_t)(micros() - lowFaultInjectionStartUs);

Serial.print("FAULT_INJECTION_TIMING | Status: STUCK_LOW");

Serial.print(" | From command to detection: ");

Serial.print(elapsedUs);

Serial.println(" us");

}

// ========================================

// FINISH MEASUREMENT

// ========================================

void finishMeasurement(

uint32_t distance,

SensorStatus status

) {

publishSnapshot(distance, status);

driverState = IDLE;

}

// ========================================

// PROCESS DISTANCE

// ========================================

void processDistance(uint32_t distance) {

if (

distance < MIN_DISTANCE_MM ||

distance > MAX_DISTANCE_MM

) {

finishMeasurement(0, OUT_OF_RANGE);

return;

}

if (isOutlier(distance)) {

finishMeasurement(0, OUTLIER_REJECTED);

return;

}



// Start timing at the first accepted raw measurement that moves

// more than 100 mm from the previous accepted raw measurement.

if (hasAcceptedReading && !filterLatencyTracking && !filterLatencyReported) {

uint32_t rawChange = (distance > lastAcceptedDistance)

? (distance - lastAcceptedDistance)

: (lastAcceptedDistance - distance);

if (rawChange > 100U) {

SensorSnapshot beforeChange = readSnapshot();

filterLatencyBaselineMm = beforeChange.distance_mm;

filterLatencyStartUs = micros();

filterLatencyTracking = true;

Serial.print("FILTER_LATENCY | Raw transition detected | Baseline: ");

Serial.print(filterLatencyBaselineMm);

Serial.print(" mm | New raw: ");

Serial.print(distance);

Serial.println(" mm");

}

}



uint32_t filteredDistance =

applyMedianFilter(distance);



// Stop timing when the filtered output has moved >100 mm from baseline.

if (filterLatencyTracking) {

uint32_t filteredChange = (filteredDistance > filterLatencyBaselineMm)

? (filteredDistance - filterLatencyBaselineMm)

: (filterLatencyBaselineMm - filteredDistance);

if (filteredChange > 100U) {

uint32_t latencyUs = (uint32_t)(micros() - filterLatencyStartUs);

Serial.print("FILTER_LATENCY_RESULT | N=3 | Latency: ");

Serial.print(latencyUs);

Serial.println(" us");

filterLatencyTracking = false;

filterLatencyReported = true;

}

}



lastAcceptedDistance = distance;

hasAcceptedReading = true;

consecutiveLowTimeouts = 0;

finishMeasurement(

filteredDistance,

SENSOR_OK

);

}

// ========================================

// PRINT TEST SNAPSHOT

// ========================================

void printTestSnapshot(const char* label) {

SensorSnapshot snapshot = readSnapshot();

Serial.print(label);

Serial.print(" | Status: ");

Serial.print(statusText(snapshot.status));

Serial.print(" | Distance: ");

Serial.print(snapshot.distance_mm);

Serial.println(" mm");

}

// ========================================

// CONTROLLED OUTLIER TEST

// ========================================

void runOutlierDemo() {

if (driverState != IDLE) {

Serial.println("TEST BUSY: Wait for sensor cycle.");

return;

}

Serial.println();

Serial.println("================================");

Serial.println(" CONTROLLED OUTLIER TEST");

Serial.println("================================");

// Establish a known accepted baseline.

lastAcceptedDistance = 1400;

hasAcceptedReading = true;

filterValues[0] = 1400;

filterValues[1] = 1400;

filterValues[2] = 1400;

filterCount = 3;

filterIndex = 0;

publishSnapshot(1400, SENSOR_OK);

printTestSnapshot("BASELINE 1400 mm");

// Inject one artificial 80 mm spike.

processDistance(80);

printTestSnapshot("INJECTED 80 mm");

// Verify recovery to the original distance.

processDistance(1400);

printTestSnapshot("RECOVERY 1400 mm");

Serial.println("================================");

Serial.println(" OUTLIER TEST SEQUENCE COMPLETE");

Serial.println("================================");

Serial.println();

}

// ========================================

// SERIAL COMMAND HANDLER

// ========================================

void handleSerialCommands() {

while (Serial.available() > 0) {

char command = Serial.read();

if (command == 'I' || command == 'i') {

if (driverState == IDLE && !highFaultInjectionArmed &&

!lowFaultInjectionArmed && !lowFaultInjectionActive) {

runOutlierDemo();

} else {

Serial.println("Wait: sensor/fault test is in progress.");

}

} else if (command == 'H' || command == 'h') {

if (driverState != IDLE || lowFaultInjectionArmed ||

lowFaultInjectionActive || highFaultInjectionArmed) {

Serial.println("HIGH injection unavailable: driver/test busy.");

} else {

highFaultInjectionStartUs = micros();

highFaultInjectionArmed = true;

Serial.println();

Serial.println("SOFTWARE FAULT INJECTION: STUCK_HIGH ARMED");

Serial.println("Waiting for driver to detect the injected HIGH...");

}

} else if (command == 'L' || command == 'l') {

if (driverState != IDLE || highFaultInjectionArmed ||

lowFaultInjectionArmed || lowFaultInjectionActive) {

Serial.println("LOW injection unavailable: driver/test busy.");

} else {

consecutiveLowTimeouts = 0;

lowFaultInjectionStartUs = micros();

lowFaultInjectionArmed = true;

Serial.println();

Serial.println("SOFTWARE FAULT INJECTION: STUCK_LOW ARMED");

Serial.println("Two simulated missing Echo-rise cycles will be tested.");

}

}

}

}



// ========================================

// NON-BLOCKING SENSOR DRIVER

// ========================================

void updateSensorDriver() {

uint32_t now = micros();

bool rise;

bool fall;

uint32_t riseTime;

uint32_t fallTime;

switch (driverState) {

case IDLE:

if (lowFaultInjectionArmed) {

lowFaultInjectionActive = true;

lowFaultInjectionArmed = false;

}

if (

highFaultInjectionArmed ||

lowFaultInjectionActive ||

(uint32_t)(now - lastTriggerUs) >= TRIGGER_INTERVAL_US

) {

if (highFaultInjectionArmed || digitalRead(ECHO_PIN) == HIGH) {

if (highFaultInjectionArmed) {

logHighInjectionTiming();

highFaultInjectionArmed = false;

}

lastTriggerUs = now;

finishMeasurement(0, STUCK_HIGH);

break;

}

resetEchoFlags();

triggerPulseDone = false;
triggerStartUs = now;
lastTriggerUs = now;
driverState = TRIGGERING;

if (!triggerRmtReady ||
    rmt_write_items(TRIG_RMT_CHANNEL, &triggerRmtItem, 1, false) != ESP_OK) {
  Serial.println("TRIG_RMT_ERROR | Hardware trigger pulse could not start");
  driverState = IDLE;
}

}

break;

case TRIGGERING:

// RMT hardware ends the 10 us pulse independently of loop scheduling.
if (triggerPulseDone) {
  triggerPulseDone = false;
  phaseStartUs = now;
  driverState = WAIT_RISE;
} else if ((uint32_t)(now - triggerStartUs) >= 1000U) {
  // Defensive recovery if the RMT completion interrupt is not received.
  Serial.println("TRIG_RMT_ERROR | Completion timeout");
  driverState = IDLE;
}

break;

case WAIT_RISE:

readEchoFlags(

rise,

fall,

riseTime,

fallTime

);

// During controlled LOW injection, ignore any Echo rise.

if (lowFaultInjectionActive) {

rise = false;

}

if (rise) {

consecutiveLowTimeouts = 0;

phaseStartUs = now;

driverState = WAIT_FALL;

}

else if (

(uint32_t)(now - phaseStartUs)

>= RISE_TIMEOUT_US

) {

if (lowFaultInjectionActive || digitalRead(ECHO_PIN) == LOW) {

consecutiveLowTimeouts++;

if (consecutiveLowTimeouts >= 2) {

if (lowFaultInjectionActive) {

logLowInjectionTiming();

lowFaultInjectionActive = false;

}

finishMeasurement(0, STUCK_LOW);

} else {

finishMeasurement(0, NO_ECHO_TIMEOUT);

}

} else {

finishMeasurement(0, STUCK_HIGH);

}

}

break;

case WAIT_FALL:

readEchoFlags(

rise,

fall,

riseTime,

fallTime

);

if (fall) {

uint32_t pulseWidth =

calculateEchoWidthUs(

riseTime,

fallTime

);

// Integer conversion with nearest rounding.

uint32_t distance =

(pulseWidth * 10U + 29U) / 58U;

processDistance(distance);

}

else if (

(uint32_t)(now - phaseStartUs)

>= FALL_TIMEOUT_US

) {

finishMeasurement(0, STUCK_HIGH);

}

break;

}

}

// ========================================

// SERIAL OUTPUT

// ========================================

void printSnapshot() {

SensorSnapshot snapshot = getCurrentSnapshot();

uint32_t age =

getMeasurementAgeUs(snapshot);

Serial.print("Snapshot Seq: ");

Serial.print(snapshot.seq);

Serial.print(" | Status: ");

Serial.print(statusText(snapshot.status));

Serial.print(" | Distance: ");

Serial.print(snapshot.distance_mm);

Serial.print(" mm | Age: ");

Serial.print(age);

Serial.print(" us | Invalid count: ");

Serial.print(invalidCount);

if (sensorFault) {

Serial.print(" | SENSOR_FAULT");

}

Serial.println();

}

// ========================================

// SETUP

// ========================================

void setup() {

Serial.begin(115200);

pinMode(ECHO_PIN, INPUT);

triggerRmtReady = initializeTriggerRmt();
if (!triggerRmtReady) {
  Serial.println("TRIG_RMT_ERROR | Initialization failed");
}

attachInterrupt(

digitalPinToInterrupt(ECHO_PIN),

echoISR,

CHANGE

);

lastTriggerUs =

micros() - TRIGGER_INTERVAL_US;

Serial.println();

Serial.println("================================");

Serial.println("          ECHO SENTINEL");

Serial.println("   Distrustful Ultrasonic Driver");

Serial.println("================================");

Serial.println("Sensor driver initialized.");
Serial.println("TRIG generator: ESP32 RMT hardware, 1 MHz tick, 10 us HIGH pulse");

Serial.println("Median filter: N=3");

Serial.println("Outlier threshold: 500 mm");

Serial.println("Send I: controlled outlier test");

Serial.println("Send H: software STUCK_HIGH timing test");

Serial.println("Send L: software STUCK_LOW timing test");

Serial.println("M1.8: move target distance; filter latency is logged automatically");

}

// ========================================

// MAIN LOOP

// ========================================

void loop() {

updateSensorDriver();

handleSerialCommands();

uint32_t now = micros();

if (

(uint32_t)(now - lastPrintUs)

>= 500000

) {

lastPrintUs = now;

printSnapshot();

}

}
