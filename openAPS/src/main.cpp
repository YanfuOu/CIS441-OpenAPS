#include <Arduino.h>
#include <vector>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

const char MQTT_USERNAME[] = "cis541-2024";
const char MQTT_PASSWORD[] = "cukwy2-geNwit-puqced";
const char ssid[] = "AirPennNet-Device";
const char pass[] = "penn1740wifi";
char buf[100];

//connecting to the broker
const char broker[] = "mqtt-dev.precise.seas.upenn.edu";
const int port = 1883;

const char OpenAPS_topic1[] = "cis541-2024/Team04/insulin-pump-openaps";
const char OpenAPS_topic3[] = "cis541-2024/Team04/cgm-openaps"; 

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);


/*
void publishInsulin() {

  String payload = "Test sending insulin";
  payload += String(send_ct); 
  send_ct++; 
  Serial.print("Sending message to the following topic: ");
  Serial.println(OpenAPS_topic1);

  Serial.print("with the following payload: "); 
  Serial.println(payload);

  mqttClient.beginMessage(OpenAPS_topic1, payload.length(), retained, qos, myDup);
  mqttClient.print(payload);
  mqttClient.endMessage();

}
*/


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
                    // may be some issues here
                    activity = peak_val - (peak_val / (treatment.duration - peak_time)) * timeAfterPeak;
                }
                total_activity += activity;
                // wot
                float percent_remaining = (float)(treatment.duration - time_since_treatment)/ treatment.duration;
                total_iob += percent_remaining * treatment.amount; 
            }
        }

        return {total_activity, total_iob};
    }

    std::pair<float, float> get_BG_forecast(float current_BG, float activity, float IOB) {
        // TODO: Implement blood glucose forecasting
        // Return pair of naive_eventual_BG and eventual_BG
            // Constants for the impact of IOB and activity on blood glucose
        const float IOB_EFFECT = 30.0f; // Each unit of IOB lowers BG by 30 mg/dL (this is an example value)
        const float ACTIVITY_EFFECT = 0.5f; // Activity lowers BG by 0.5 * activity level (example coefficient)

        // Calculate naive BG based on IOB alone
        float naive_eventual_BG = current_BG - (IOB * IOB_EFFECT);

        // Calculate the effect of activity on BG
        float activity_reduction = activity * ACTIVITY_EFFECT;


        // Calculate the eventual BG considering both IOB and activity
        float eventual_BG = naive_eventual_BG - activity_reduction;

        /*
        float naive_eventual_BG = current_BG - (IOB * ISF);
        float predBGI = -activity * ISF * 5;
        float delta = current_BG - prev_BG;
        float deviation = 30 / 5 * (delta - predBGI);

        float eventual_BG = naive_eventual_BG - deviation;
        */

        return std::make_pair(naive_eventual_BG, eventual_BG);

    }

    float get_basal_rate(long t, float current_BG) {
      // get calculations from previous functions
      std::pair<float, float> calculations = insulin_calculations(t);
      float total_activity = calculations.first;
      float total_iob = calculations.second;
      std::pair<float, float> bg_forecast = get_BG_forecast(current_BG, total_activity, total_iob);
      float naive_eventual_BG = bg_forecast.first;
      float eventual_BG = bg_forecast.second;

      // calculate basal rate (from slides)
      float basal_rate = 0;
      if (current_BG < threshold_BG || eventual_BG < threshold_BG) {
        basal_rate = 0;
      } else if (eventual_BG < target_BG) {
        if (naive_eventual_BG < 40) {
          basal_rate = 0;
        }
        float insulinReq = 2 * (eventual_BG - target_BG) / ISF;
        basal_rate = prev_basal_rate + (insulinReq / DIA);
      } else if (eventual_BG > target_BG) {
        float insulinReq = 2 * (eventual_BG - target_BG) / ISF;
        basal_rate = prev_basal_rate + (insulinReq / DIA);
      }

      // update prev bg, basal rate, and add new insulin treatment
      prev_BG = current_BG;
      prev_basal_rate = basal_rate;
      InsulinTreatment treatment = {t, basal_rate * DIA, DIA};
      addInsulinTreatment(treatment);
      return basal_rate;
    }
};

OpenAPS *oa;

void TaskMQTT(void *pvParameters) {
    // TODO: Implement MQTT task
    // Continuously poll for MQTT messages
    int messageSize = mqttClient.parseMessage();
    if (messageSize) {
      // we received a message, print out the topic and contents
      Serial.print("Received a message with topic '");
      Serial.print(mqttClient.messageTopic());
      Serial.print("', length ");
      Serial.print(messageSize);
      Serial.println(" bytes:");

      // use the Stream interface to print the contents
      String payload = "";
      while (mqttClient.available()) {
        payload += (char)mqttClient.read();
      }
      Serial.println(payload);
      // parsing the payload to get blood glucose and time
      int speedPos = payload.indexOf("Glucose");
      if (speedPos != -1) {
        int colonPos = payload.indexOf(':', speedPos);
        int commaPos = payload.indexOf(',', colonPos);
        int endPos = payload.indexOf('}', colonPos);
        String speedStr = payload.substring(colonPos + 2, commaPos);
        current_BG = speedStr.toDouble();
        //double newSpeed = speedStr.toDouble();
        String timeStr = payload.substring(commaPos + 10, endPos);
        current_time = timeStr.toInt();
      }
      Serial.println(current_BG);
      Serial.println(current_time);
      // set global variables for global glucse and time

      Serial.println();
    }
}

void TaskOpenAPS(void *pvParameters) {
  // get basal rate
  float basal_rate = (*oa).get_basal_rate(current_time, current_BG);

  // publish
  sprintf(buf, "{\"Glucose\":%.9f}", basal_rate);
  Serial.println(buf);
  size_t len = strlen(buf);
  mqttClient.beginMessage(OpenAPS_topic1, len, false, 1, false);
  mqttClient.print(buf);
  mqttClient.endMessage();
}

/*
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
*/

void setup() {
    Serial.begin(9600);
    Serial.print("setting up");
    // Connect to Wifi
    Serial.print("---------Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    Serial.println("--------");
    while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
      Serial.print("Attempting to connect to WiFi....");
      delay(5000);
    }
    Serial.println("\nYou're connected to the network! YAYAYAA");

    mqttClient.setUsernamePassword(MQTT_USERNAME, MQTT_PASSWORD);

    if (!mqttClient.connect(broker, port)) {
      Serial.print("MQTT connection failed! Error code = ");
      Serial.println(mqttClient.connectError());

      while (1);
    }
    Serial.println("You're connected to the MQTT broker!\n");

    //I need to subscribe to topic 3 and publish to topic 1
    // Subscrie to the CGM topic
    Serial.print("Subscribing to: ");
    Serial.println(OpenAPS_topic3);
    int ret = mqttClient.subscribe(OpenAPS_topic3, 1);
    sprintf(buf, "%d", ret);
    Serial.println(buf);

    // xTaskCreate(TaskMQTT, "Task1", 1024, NULL, 1, NULL);

    // Set callback for incoming messages
    // TODO: Implement setup function
    // Initialize Serial, WiFi, MQTT, OpenAPS, mutex, and tasks
    // Subscribe to necessary MQTT topics
    // Request virtual patient profile
    //---------------MQTT part ----------
}

void loop() {
    // Empty. Tasks are handled by FreeRTOS
    //mqttClient.poll();
    //mqttClient.onMessage(onMqttMessage);
    //publishInsulin(); 
    TaskMQTT(NULL);
    //TaskOpenAPS(NULL);
}
