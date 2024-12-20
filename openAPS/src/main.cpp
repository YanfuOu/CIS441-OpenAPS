#include <Arduino.h>
#include <vector>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <string>

const char MQTT_USERNAME[] = "cis541-2024";
const char MQTT_PASSWORD[] = "cukwy2-geNwit-puqced";
const char ssid[] = "YanfuOu";
const char pass[] = "yanfuthebest";
char buf[100];

//connecting to the broker
const char broker[] = "mqtt-dev.precise.seas.upenn.edu";
const int port = 1883;

const char OpenAPS_topic1[] = "cis541-2024/Team04/insulin-pump-openaps";
const char OpenAPS_topic3[] = "cis541-2024/Team04/cgm-openaps"; 
const char patient_profile_topic1[] = "tb-proxy/8yj0bip0cpgcurdbgzry/attributes/request/1";
const char patient_profile_topic2[] = "tb-proxy/8yj0bip0cpgcurdbgzry/attributes/response/1";

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

float dosage_amt = 0.0;
SemaphoreHandle_t mutex;

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
        float predBGI = -activity * ISF * 5.0;
        float delta = current_BG - prev_BG;
        float deviation = 30.0 / 5.0 * (delta - predBGI);

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
      float basal_rate = 0.0;
      Serial.println("GEt basal rate func");
      Serial.println(current_BG);
      Serial.println(eventual_BG);
      Serial.println(naive_eventual_BG);

      if (eventual_BG < 50.0) {
        basal_rate = 0.0;
        Serial.println("here1");

      } else if (eventual_BG < 100.0) {
        if (naive_eventual_BG < 40.0) {
          Serial.println("here2");
          basal_rate = 0.0;
        }
        Serial.println(basal_rate);
        float difference = eventual_BG - 50.0;
        float insulinReq = 2.0 * difference;
        insulinReq = insulinReq / 5.0;
        float incr = insulinReq / 90.0;
        basal_rate += incr;
        basal_rate += insulinReq / 90.0;
      } else if (eventual_BG > 100.0) {
        float difference = eventual_BG - 100.0;
        Serial.println(difference);
        float insulinReq = 2.0 * difference;
        Serial.println(insulinReq);
        insulinReq = insulinReq / 5.0;
        Serial.println(insulinReq);
        insulinReq = insulinReq / 10.0;
        Serial.println(insulinReq);
        float incr = insulinReq / 90.0;
        Serial.println(incr);
        basal_rate += incr;
      }

      // update prev bg, basal rate, and add new insulin treatment
      prev_BG = current_BG;
      prev_basal_rate = basal_rate;
      InsulinTreatment treatment = {t, basal_rate * DIA, DIA};
      addInsulinTreatment(treatment);
      Serial.println("Basal rate");
      Serial.println(basal_rate);
      return basal_rate;
    }
};

OpenAPS *oa;

void TaskMQTT(void *pvParameters) {
  while(!attributeReceived);
  while (1) {
    Serial.println("polling");
    String payload = "";
    mqttClient.poll();
    vTaskDelay(30);
    while (!mqttClient.available()) {
      mqttClient.poll();
      vTaskDelay(30);
    }
    while (mqttClient.available()) {
      payload += (char)mqttClient.read();
    }
    Serial.println(payload);
    // parsing the payload to get blood glucose and time
    int speedPos = payload.indexOf("Glucose");
    if (speedPos != -1) {
      Serial.println("in payload parser");
      int colonPos = payload.indexOf(':', speedPos);
      int commaPos = payload.indexOf(',', colonPos);
      int endPos = payload.indexOf('}', colonPos);
      String speedStr = payload.substring(colonPos + 2, commaPos);
      String timeStr = payload.substring(commaPos + 10, endPos);
      current_BG = speedStr.toFloat();
      current_time = timeStr.toInt();
    }
    // int messageSize = mqttClient.parseMessage();
    // if (messageSize) {
    //   // we received a message, print out the topic and contents
    //   Serial.print("Received a message with topic '");
    //   Serial.print(mqttClient.messageTopic());
    //   Serial.print("', length ");
    //   Serial.print(messageSize);
    //   Serial.println(" bytes:");

    //   // use the Stream interface to print the contents
    //   String payload = "";
    //   while (mqttClient.available()) {
    //     payload += (char)mqttClient.read();
    //   }
    //   Serial.println(payload);
    //   // parsing the payload to get blood glucose and time
    //   int speedPos = payload.indexOf("Glucose");
    //   if (speedPos != -1) {
    //     Serial.println("in payload parser");
    //     int colonPos = payload.indexOf(':', speedPos);
    //     int commaPos = payload.indexOf(',', colonPos);
    //     int endPos = payload.indexOf('}', colonPos);
    //     String speedStr = payload.substring(colonPos + 2, commaPos);
    //     String timeStr = payload.substring(commaPos + 10, endPos);
    //     current_BG = speedStr.toFloat();
    //     current_time = timeStr.toInt();
    //   }
    // }
    // publish
    xSemaphoreTake(mutex, portMAX_DELAY);
    sprintf(buf, "{\"insulin_rate\":%.9f}", dosage_amt);
    xSemaphoreGive(mutex);
    Serial.println(buf);
    size_t len = strlen(buf);
    mqttClient.beginMessage(OpenAPS_topic1, len, false, 1, false);
    mqttClient.print(buf);
    mqttClient.endMessage();

  }
}

void TaskOpenAPS(void *pvParameters) {
  // get basal rate
  while (!attributeReceived);
  while (1) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    dosage_amt = (*oa).get_basal_rate(current_time, current_BG) * 90.0;
    xSemaphoreGive(mutex);
    vTaskDelay(1000);
  }
}

std::vector<InsulinTreatment> extract(String s) {
  std::string str = std::string(s.c_str());

  std::size_t indexOfBolus = str.find("bolus_insulins");
  std::string bolusSub = str.substr(indexOfBolus);
  std::size_t bracket1 = bolusSub.find('[');
  std::size_t bracket2 = bolusSub.find(']');

  std::vector<InsulinTreatment> res;
  std::string bolusArr = bolusSub.substr(bracket1, bracket2 - bracket1 + 1);
  bolusArr = bolusArr.substr(bolusArr.find('{') + 1);

  std::size_t idxOfTime = bolusArr.find("time");
  std::size_t idxOfDose = bolusArr.find("dose");
  std::size_t idxOfDuration = bolusArr.find("duration");

  std::string curr = bolusArr;
  std::string temp;
  std::size_t neg1 = -1;
  while (idxOfTime != neg1) {
    std::size_t idxOfFirstComma = curr.find(',');
    std::size_t idxOfSecondComma = curr.find(',', idxOfFirstComma + 1);
    long time = std::stol(curr.substr(idxOfTime + 6, idxOfFirstComma - idxOfTime - 6));
    float dose = std::stof(curr.substr(idxOfDose + 6, idxOfSecondComma - idxOfDose - 6));
    int duration = std::stoi(curr.substr(idxOfDuration + 10, curr.find('}') - idxOfDuration - 10));
    InsulinTreatment it = InsulinTreatment(time, dose, duration);
    res.push_back(it);
    curr = curr.substr(curr.find('{') + 2);
    idxOfTime = curr.find("time");
    idxOfDose = curr.find("dose");
    idxOfDuration = curr.find("duration");
  }
  return res;
}

void setup() {
  Serial.begin(9600);
  while(!Serial);

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
  Serial.println();

  mqttClient.subscribe(patient_profile_topic2, 1);
  String payload = "{\"sharedKeys\": \"PatientProfile\"}";
  mqttClient.beginMessage(patient_profile_topic1, payload.length(), false, 1, false);
  mqttClient.print(payload);
  mqttClient.endMessage();

  payload = "";
  while (!mqttClient.available()) {
      mqttClient.poll();
      vTaskDelay(30);
  }
  while (mqttClient.available()) {
    payload += (char)mqttClient.read();
  }
  std::vector<InsulinTreatment> v = extract(payload);
  for (InsulinTreatment it : v) {
    Serial.print(it.time);
    Serial.print(", ");
    Serial.print(it.amount);
    Serial.print(", ");
    Serial.println(it.duration);
  }

  *oa = OpenAPS(v);
  attributeReceived = true;

  mutex = xSemaphoreCreateMutex();
  xTaskCreate(TaskMQTT, "Task MQTT", 1024, NULL, 1, NULL);
  xTaskCreate(TaskOpenAPS, "Task OpenAPS", 1024, NULL, 1, NULL);
  vTaskStartScheduler();
}
    


    // xTaskCreate(TaskMQTT, "Task1", 1024, NULL, 1, NULL);

    // Set callback for incoming messages
    // TODO: Implement setup function
    // Initialize Serial, WiFi, MQTT, OpenAPS, mutex, and tasks
    // Subscribe to necessary MQTT topics
    // Request virtual patient profile
    //---------------MQTT part ----------

void loop() {
    // Empty. Tasks are handled by FreeRTOS
}
