#include <Arduino.h>
#include <vector>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "arduino_secrets.h"


#include <iostream>
#include <string>
#include <unistd.h>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>

using namespace std;

/*
const string MQTT_USERNAME = "cis541-2024";
const string MQTT_PASSWORD = "cukwy2-geNwit-puqced";
*/
const char ssid[] = "YanfuOu";
const char pass[] = "yanfuthebest";

//connecting to the broker
const char broker[] = "mqtt-dev.precise.seas.upenn.edu";
const int port = 1883;

const char inTopic[] = "tb-proxy/CAR_GATEWAY/attributes";
const char subTopic[] = "tb-proxy/CAR_GATEWAY/attributes/response/+";
const char topic1[] = "tb-proxy/CAR_GATEWAY/gateway/connect";
const char topic2[] = "tb-proxy/CAR_GATEWAY/gateway/attributes";
const char speedRequestTopic[] = "tb-proxy/CAR_GATEWAY/attributes/request/";

const char OpenAPS_topic1[] = "cis541-2024/Team04/insulin-pump-openaps";
const char OpenAPS_topic3[] = "cis541-2024/Team04/cgm-openaps"; 
const char OpenAPS_topic3_subtopic[] = "cis541-2024/Team04/cgm-openaps/+";


const long positionUpdateInterval = 1000;  
const long speedRequestInterval = 5000;    
unsigned long previousMillis = 0;
unsigned long previousSpeedRequestMillis = 0;

/*
auto connOpts = mqtt::connect_options_builder()
        .clean_session()
        .automatic_reconnect()
        .user_name(MQTT_USERNAME)
        .password(MQTT_PASSWORD)
        .finalize();
*/

String willPayload = "Issues!!!";
bool willRetain = true;
int willQos = 1;
bool retained = false;
int qos = 1;
bool myDup = false;

double position = 30;
double speed = 4.0;  
int request_id = 1;  

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

void connectToWiFi() {
  Serial.print("---------Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  Serial.println("--------");
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    Serial.print("Attempting to connect to WiFi....");
    delay(5000);
  }
  Serial.println("\nYou're connected to the network! YAYAYAA");
}

void connectToMQTT() {
  mqttClient.setId("clientId");
  mqttClient.setUsernamePassword("cis541-2024", "cukwy2-geNwit-puqced");
  mqttClient.setCleanSession(false);

  mqttClient.beginWill(topic1, willPayload.length(), willRetain, willQos);
  mqttClient.print(willPayload);
  mqttClient.endWill();

  while (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());
    connectToMQTT(); 
    delay(3000);  
  }
  Serial.println("You're connected to the MQTT broker!\n");
}

void registerDevice() {
/*
auto msg = mqtt::make_message("tb-proxy/CAR_GATEWAY/gateway/connect", "{\"device\": \"" + DEVICE_ID + "\"}"); 
    msg->set_qos(QOS); 
    client.publish(msg)->wait();
    cout << "Device registered." << endl;
*/

  String payload = "{\"device\":\"3141\"}";
  Serial.print("Sending message to the following topic: ");

  Serial.println(topic1);
  
  Serial.println(payload);

  mqttClient.beginMessage(topic1, payload.length(), retained, qos, myDup);

  mqttClient.print(payload);

  mqttClient.endMessage();

  Serial.println();
}

void subscribeToTopics() {
  Serial.print("Subscribing to the following topic: ");
  Serial.println(OpenAPS_topic3);
  mqttClient.subscribe(OpenAPS_topic3, 1);

  Serial.print("Subscribing to the following topic: ");
  Serial.println(OpenAPS_topic3_subtopic);
  mqttClient.subscribe(OpenAPS_topic3_subtopic, 1);

}



void requestSpeed() {
/*
auto msg = mqtt::make_message("tb-proxy/CAR_GATEWAY/attributes/request/" + to_string(request_id++),
                                  "{\"sharedKeys\":\"Speed\"}");
    msg->set_qos(QOS);
    client.publish(msg)->wait();
    cout << "Request for speed published: " << request_id << endl;
*/


  String speedRequestPayload = "{\"sharedKeys\":\"Speed\"}";
  String speedRequestFullTopic = String(speedRequestTopic) + request_id++;

  Serial.print("Requesting the following speed with the following ID: ");
  Serial.println(request_id);

  mqttClient.beginMessage(speedRequestFullTopic.c_str(), speedRequestPayload.length(), retained, qos, myDup);
  mqttClient.print(speedRequestPayload);
  mqttClient.endMessage();

  Serial.println();
}

void onMqttMessage(int messageSize) {
  Serial.print("Receiving a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', this length ");
  Serial.print(messageSize);
  Serial.println(" and this bytes:");

  String payload;
  while (mqttClient.available()) {
    payload += (char)mqttClient.read();
  }
  Serial.println(payload);
  Serial.println();
}


//--------------------open APS part-----------

// WiFi and MQTT configuration

// Task initialization




struct InsulinTreatment {
    long time;
    float amount;
    int duration;
    InsulinTreatment(long t, float a, int d) : time(t), amount(a), duration(d) {}
};


// Global variables
//OpenAPS* openAPS;
volatile float current_BG = 0.0;
volatile long current_time = 0;
volatile bool newBGData = false;
volatile bool newInsulinTreatment = false;
volatile bool attributeReceived = false;



class OpenAPS {
private:
    float ISF = 5;
    int DIA = 90;
    int target_BG = 100;
    int threshold_BG = 50;
    std::vector<InsulinTreatment> insulin_treatments;
    float prev_BG = -1;
    float prev_basal_rate = 0.0;

public:
    OpenAPS(std::vector<InsulinTreatment> bolus_insulins) 
        : insulin_treatments(bolus_insulins) {}

    void clearInsulinTreatments() { insulin_treatments.clear(); }
    
    void addInsulinTreatment(const InsulinTreatment& treatment) { insulin_treatments.push_back(treatment); }

    std::pair<float, float> insulin_calculations(long t) {
        // TODO: Implement insulin calculations
        // Return pair of total_activity and total_iob
        float total_activity = 0;
        float total_iob = 0;
        for (InsulinTreatment treatment : insulin_treatments) {
            long time_since_treatment = t - treatment.time;
            float peak_time = (DIA * 75) / 180;
            float peak_val = (2.0 * treatment.amount) / (treatment.duration * peak_time);

            float activity = 0.0;
            if (time_since_treatment >= 0 && time_since_treatment <= treatment.duration) {
                if (time_since_treatment <= peak_time) {
                    activity = (peak_val / peak_time) * time_since_treatment;
                }
                else {
                    float timeAfterPeak = time_since_treatment - peak_time; 
                    activity = peak_val - (peak_val / (treatment.duration - peak_time)) * timeAfterPeak;
                }
                total_activity += activity;
                float percent_remaining = (float)(treatment.duration - time_since_treatment)/ treatment.duration;
                total_iob += percent_remaining * treatment.amount; 
            }
        }

        return {total_activity, total_iob};
    }

    
    

    std::pair<float, float> get_BG_forecast(float current_BG, float activity, float IOB) {
        // TODO: Implement blood glucose forecasting
        // Return pair of naive_eventual_BG and eventual_BG
        return {99.99, 88.88};

    }

    float get_basal_rate(long t, float current_BG) {
        // TODO: Implement basal rate calculation
        // Use insulin_calculations and get_BG_forecast
        // Apply control logic based on BG levels
        // Update prev_BG, prev_basal_rate, and add new insulin treatment
        // Return calculated basal_rate
        return 77.7;
    }
    
};




/* 
void onMqttMessage(int messageSize) {
    // TODO: Implement MQTT message callback
    // Handle attribute updates and CGM data
    // Update openAPS, current_BG, current_time, and flags as needed
}
*/ 
void TaskMQTT(void *pvParameters) {
    // TODO: Implement MQTT task
    // Continuously poll for MQTT messages
}

void TaskOpenAPS(void *pvParameters) {
    // TODO: Implement OpenAPS task
    // Process new data, calculate basal rate, and publish to MQTT
}

void setup() {
    Serial.begin(9600);


    Serial.print("setting up");



    // Connect to Wifi
    connectToWiFi();

    

    // Connect to MQTT
    connectToMQTT();

    //I need to subscribe to topic 3 and publish to topic 1

    // Subscrie to the CGM topic
    subscribeToTopics();

    // Set callback for incoming messages
    mqttClient.onMessage(onMqttMessage);

    /*
    // Register Car device
    registerDevice();



    // Set callback for incoming messages
    mqttClient.onMessage(onMqttMessage);

    */

    // TODO: Implement setup function
    // Initialize Serial, WiFi, MQTT, OpenAPS, mutex, and tasks
    // Subscribe to necessary MQTT topics
    // Request virtual patient profile
    //---------------MQTT part ----------



}

void loop() {
    // Empty. Tasks are handled by FreeRTOS
    mqttClient.poll();
    unsigned long currentMillis = millis();

}
