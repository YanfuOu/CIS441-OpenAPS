#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <mqtt/async_client.h>

using namespace std;
using namespace std::chrono;

const string ADDRESS { "tcp://mqtt-dev.precise.seas.upenn.edu" };
const string USERNAME { "cis541-2024" };
const string PASSWORD { "cukwy2-geNwit-puqced" };

const int QOS = 1;

// communication between Virtual Compenent and Virtual Patient
const string INSULIN_TOPIC { "cis541-2024/Team04/insulin-pump" };
const string CGM_TOPIC { "cis541-2024/Team04/cgm" };

// communication between Virtual Compenent and OpenAPS
const string OA_INSULIN_TOPIC { "cis541-2024/Team04/insulin-pump-openaps" };
const string OA_CGM_TOPIC { "cis541-2024/Team04/cgm-openaps" };

const auto TIMEOUT = std::chrono::seconds(5);

char msgContents[25];

// Separate callback class inheriting from mqtt::callback
class MessageRelayCallback : public virtual mqtt::callback {
    mqtt::async_client& client_;

public:
    MessageRelayCallback(mqtt::async_client& client)
        : client_(client) {}

    // subscribe mqtt topics
    void connected(const string& cause) override {
        std::cout << "\nConnection success" << std::endl;
        std::cout << "\nSubscribing to topic '" << CGM_TOPIC << "'\n";
        client_.subscribe(CGM_TOPIC, QOS);
        std::cout << "\nSubscribing to topic '" << OA_INSULIN_TOPIC << "'\n";
        client_.subscribe(OA_INSULIN_TOPIC, QOS);
    }

    // message handler
    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message arrived" << std::endl;
		std::cout << "\ttopic: '" << msg->get_topic() << "'" << std::endl;
		std::cout << "\tpayload: '" << msg->to_string() << "'\n" << std::endl;
        if (msg->get_topic() == "cis541-2024/Team04/cgm") {
            on_message_cgm(msg->to_string());
        } else {
            on_message_insulin(msg->to_string());
        }
    }

    // handle cgm message
    void on_message_cgm(const string& payload) {
        std::cout << "Sending: " << payload << " to " << OA_CGM_TOPIC << std::endl;
        auto msg = mqtt::make_message(OA_CGM_TOPIC, payload);
        msg->set_qos(QOS);
        client_.publish(msg)->wait_for(TIMEOUT);
    }

    // handle insulin message
    void on_message_insulin(const string& payload) {
        auto msg = mqtt::make_message(INSULIN_TOPIC, payload);
        msg->set_qos(QOS);
        client_.publish(msg)->wait_for(TIMEOUT);
    }

    
};


// The main MQTTClientHandler class to manage the connection and the callback
class MQTTClientHandler {
    mqtt::async_client client_;
    MessageRelayCallback callback_;

public:
    MQTTClientHandler(const string& host, const string& username, const string& password)
        : client_(host, ""), callback_(client_) {

        // connect to mqtt
        // Username & Password
        auto connOpts = mqtt::connect_options_builder()
            .clean_session()
            .automatic_reconnect()
            .user_name(USERNAME)
            .password(PASSWORD)
            .finalize();
        // Perform connection
        client_.set_callback(callback_);
        try {
            cout << "Connecting..." << endl;
            auto connTok = client_.connect(connOpts);
        }
        catch(const mqtt::exception& ex) {
            cerr << "\nERROR: unable to connect, " << ex << endl;
        }
    }

    // This keeps the program running indefinitely, which ensures that the MQTT client remains active and ready to receive and send messages.
    void inject_loop() {
        cout << "Press Ctrl+C to exit the mqtt handler." << endl;
        while (true) {
            this_thread::sleep_for(seconds(1));
        }
    }
};

int main() {

    MQTTClientHandler mqtt_handler(ADDRESS, USERNAME, PASSWORD);
    mqtt_handler.inject_loop();

    return 0;
}