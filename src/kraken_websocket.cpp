// stupid ahh deprecated rapidjson
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "kraken_websocket.h"

using namespace rapidjson;

class kraken_ws_client {
private:
  CURL *curl;
  string url = "wss://ws.kraken.com";
  bool connected = false;
  thread ws_thread;
  string response_data;

public:
  kraken_ws_client() {
    curl = curl_easy_init();
    if (!curl) {
      throw runtime_error("Failed to initialize curl");
    }
  }

  ~kraken_ws_client() {
    connected = false;
    if (curl) {
      curl_easy_cleanup(curl);
    }
    if (ws_thread.joinable()) {
      ws_thread.join();
    }
  }

  void connect() {
    ws_thread = thread([this]() {
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);

      CURLcode res = curl_easy_perform(curl);
      if (res == CURLE_OK) {
        connected = true;
        cout << "WebSocket connection opened" << endl;

        // Send subscription message
        subscribe_to_ticker("ETH/USD");

        // Keep connection alive and process messages
        while (connected) {
          receive_messages();
          this_thread::sleep_for(chrono::milliseconds(100));
        }
      } else {
        cout << "WebSocket connection failed: " << curl_easy_strerror(res)
             << endl;
      }
    });

    // Wait for connection
    this_thread::sleep_for(chrono::seconds(2));
  }

  void subscribe_to_ticker(const string &pair) {
    if (!connected) {
      cout << "Not connected to WebSocket" << endl;
      return;
    }

    Document subscription;
    subscription.SetObject();
    Document::AllocatorType &allocator = subscription.GetAllocator();

    // Add event
    Value eventValue("subscribe", allocator);
    subscription.AddMember("event", eventValue, allocator);

    // Add pair array
    Value pairArray(kArrayType);
    Value pairValue(pair.c_str(), allocator);
    pairArray.PushBack(pairValue, allocator);
    subscription.AddMember("pair", pairArray, allocator);

    // Add subscription object
    Value subscriptionObj(kObjectType);
    Value nameValue("ticker", allocator);
    subscriptionObj.AddMember("name", nameValue, allocator);
    subscription.AddMember("subscription", subscriptionObj, allocator);

    // Serialize to JSON
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    subscription.Accept(writer);

    string message = buffer.GetString();

    size_t sent;
    CURLcode res = curl_ws_send(curl, message.c_str(), message.length(), &sent,
                                0, CURLWS_TEXT);

    if (res != CURLE_OK) {
      cout << "Send error: " << curl_easy_strerror(res) << endl;
    }
  }

  void receive_messages() {
    char buffer[1024];
    size_t received;
    const struct curl_ws_frame *frame;

    CURLcode res =
        curl_ws_recv(curl, buffer, sizeof(buffer), &received, &frame);
    if (res == CURLE_OK && received > 0) {
      string message(buffer, received);
      on_message(message);
    }
  }

  void on_message(const string &message) {
    Document root;
    root.Parse(message.c_str());

    if (!root.HasParseError()) {
      // Handle ticker data
      if (root.IsArray() && root.Size() > 3) {
        const Value &ticker_data = root[1];
        if (ticker_data.IsObject() && ticker_data.HasMember("c")) {
          const Value &price_array = ticker_data["c"];
          if (price_array.IsArray() && price_array.Size() > 0) {
            string last_price = price_array[0].GetString();
            string pair = root[3].GetString();
            cout << "Price update for " << pair << ": $" << last_price << endl;
          }
        }
      }
      // Handle subscription confirmation
      else if (root.IsObject() && root.HasMember("event") &&
               string(root["event"].GetString()) == "subscriptionStatus") {
        cout << "Subscription status: " << root["status"].GetString() << endl;
      }
    }
  }

  void run() {
    connect();

    // Keep running
    while (connected) {
      this_thread::sleep_for(chrono::seconds(1));
    }
  }
};
