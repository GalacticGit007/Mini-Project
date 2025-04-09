#include <Abhinavta-project-1_inferencing.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <MAX30100_PulseOximeter.h>

// Wi-Fi credentials
const char* ssid = "Poco";
const char* password = "Abhi12345";

// IFTTT Settings
const char* iftttWebhookURL = "https://maker.ifttt.com/trigger/Fall/with/key/bJwgD4cwrNtbEHJMGsCQsGmxfg0s2yHRHxoT_NwRWcQ";
unsigned long lastTriggerTime = 0;
const long cooldownPeriod = 30000; // 30 seconds cooldown

// Sensor Objects
PulseOximeter pox;
Adafruit_MPU6050 mpu;

// Sensor Data
float beatsPerMinute = 0;
float SpO2 = 0;
static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

// Track time for MAX30100 updates
unsigned long lastPoxUpdate = 0;
const unsigned long poxUpdateInterval = 10; // Update MAX30100 every 10ms

void onBeatDetected() {
    Serial.println("Beat detected!");
}

// Initialize MAX30100 safely
void initMAX30100() {
    Serial.println("Initializing MAX30100...");
    if (!pox.begin()) {
        Serial.println("MAX30100 init failed!");
        while (1);
    }
    pox.setOnBeatDetectedCallback(onBeatDetected);
    pox.setIRLedCurrent(MAX30100_LED_CURR_24MA); // Adjust LED current
}

// Capture motion data from MPU6050
void read_sensor_data() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    features[0] = a.acceleration.x;
    features[1] = a.acceleration.y;
    features[2] = a.acceleration.z;
    features[3] = g.gyro.x;
    features[4] = g.gyro.y;
    features[5] = g.gyro.z;
}

// Ensure MAX30100 is updated regularly
void updateMAX30100() {
    if (millis() - lastPoxUpdate >= poxUpdateInterval) {
        pox.update();
        beatsPerMinute = pox.getHeartRate();
        SpO2 = pox.getSpO2();
        lastPoxUpdate = millis();
    }
}

// Ensure motion and heart rate don't block each other
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));m;ckmp
    return 0;
}

// Send an alert via IFTTT
void send_fall_alert(float bpm, float spo2) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = String(iftttWebhookURL) + "?value1=" + String(bpm) + "&value2=" + String(spo2);
        http.begin(url);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            Serial.println("Alert sent with vital signs!");
        } else {
            Serial.printf("Request failed: %d\n", httpCode);
        }
        http.end();
    } else {
        Serial.println("WiFi disconnected");
    }
}

// Capture valid heart rate and SpO2 data
void capture_max30100_data(float &bpm, float &spo2, unsigned long durationMs) {
    unsigned long startTime = millis();
    bpm = 0;
    spo2 = 0;

    pinMode(2, OUTPUT);

    while (millis() - startTime < durationMs) {
        digitalWrite(2, HIGH); // Turn on LED
        updateMAX30100();

        if (beatsPerMinute > 0 && SpO2 > 0) {
            bpm = beatsPerMinute;
            spo2 = SpO2;
            Serial.printf("HR: %.1f bpm, SpO2: %.1f%%\n", bpm, spo2);
        } else {
            Serial.println("Waiting for valid sensor data...");
        }

        delay(100); // Slow sampling to prevent interference
    }

    digitalWrite(2, LOW); // Turn off LED
}

// Handle fall detection and send alert
void handle_inference_result(ei_impulse_result_t result) {
    if (result.classification[0].value > 0.85) {
        if (millis() - lastTriggerTime > cooldownPeriod) {
            Serial.println("FALL DETECTED! Collecting vital signs...");
            float bpm = 0, spo2 = 0;
            capture_max30100_data(bpm, spo2, 20000); // Collect for 20 seconds
            send_fall_alert(bpm, spo2);
            lastTriggerTime = millis();
        }
    }

    ei_printf("Predictions (DSP: %d ms., Classification: %d ms.): \n", result.timing.dsp, result.timing.classification);
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    // Connect to WiFi
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected!");

    // Initialize MPU6050
    if (!mpu.begin()) {
        Serial.println("MPU6050 init failed!");
        while (1);
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    // Initialize MAX30100
    initMAX30100();
}

void loop() {
    // Maintain WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        delay(5000);
    }

    // Update MAX30100 without blocking
    updateMAX30100();

    // Read motion data
    read_sensor_data();

    // Run inference
    if (sizeof(features) / sizeof(float) == EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        ei_impulse_result_t result = {0};
        signal_t features_signal;

        features_signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
        features_signal.get_data = &raw_feature_get_data;

        EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);
        if (res == EI_IMPULSE_OK) {
            handle_inference_result(result);
        }
    }
}
