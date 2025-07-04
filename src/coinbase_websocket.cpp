// stupid ahh deprecated rapidjson
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "coinbase_websocket.h"

using namespace rapidjson;

struct OrderbookEntry {
  double price;
  double size;
  string timestamp;
};

struct TickerData {
  string product_id;
  double price;
  double size;
  string time;
  double best_bid;
  double best_ask;
};

class coinbase_ws_client {
private:
  CURL *curl;
  string url = "wss://advanced-trade-ws.coinbase.com";
  bool connected = false;
  thread ws_thread;
  string response_data;
  string message_buffer;
  string api_key;
  string api_secret;

  vector<OrderbookEntry> bids;
  vector<OrderbookEntry> asks;
  TickerData latest_ticker;

public:
  coinbase_ws_client() {
    curl = curl_easy_init();
    if (!curl) {
      throw runtime_error("Failed to initialize curl");
    }
  }

  ~coinbase_ws_client() {
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
        cout << "Coinbase WebSocket connection opened" << endl;

        subscribe_to_channels("BTC-USD");

        while (connected) {
          receive_messages();
          this_thread::sleep_for(chrono::milliseconds(100));
        }
      } else {
        cout << "Coinbase WebSocket connection failed: "
             << curl_easy_strerror(res) << endl;
      }
    });

    this_thread::sleep_for(chrono::seconds(2));
  }

  void subscribe_to_channels(const string &product_id) {
    if (!connected) {
      cout << "Not connected to WebSocket" << endl;
      return;
    }

    // Subscribe to level2 channel
    subscribe_to_channel(product_id, "level2");

    // Subscribe to ticker channel
    subscribe_to_channel(product_id, "ticker");
  }

  void subscribe_to_channel(const string &product_id, const string &channel) {
    Document subscription;
    subscription.SetObject();
    Document::AllocatorType &allocator = subscription.GetAllocator();

    subscription.AddMember("type", Value("subscribe", allocator), allocator);

    Value product_ids(kArrayType);
    product_ids.PushBack(Value(product_id.c_str(), allocator), allocator);
    subscription.AddMember("product_ids", product_ids, allocator);

    subscription.AddMember("channel", Value(channel.c_str(), allocator),
                           allocator);

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    subscription.Accept(writer);

    string message = buffer.GetString();
    cout << "Sending " << channel << " subscription: " << message << endl;

    size_t sent;
    CURLcode res = curl_ws_send(curl, message.c_str(), message.length(), &sent,
                                0, CURLWS_TEXT);

    if (res != CURLE_OK) {
      cout << "Send error for " << channel << ": " << curl_easy_strerror(res)
           << endl;
    }
  }

  void receive_messages() {
    char buffer[4096];
    size_t received;
    const struct curl_ws_frame *frame;

    CURLcode res =
        curl_ws_recv(curl, buffer, sizeof(buffer), &received, &frame);
    if (res == CURLE_OK && received > 0) {
      string fragment(buffer, received);

      // Add fragment to buffer
      message_buffer += fragment;

      // Look for complete JSON messages
      size_t pos = 0;
      while (pos < message_buffer.length()) {
        // Find the start of a JSON object
        size_t start = message_buffer.find('{', pos);
        if (start == string::npos)
          break;

        // Parse from the opening brace
        int brace_count = 0;
        bool in_string = false;
        char prev_char = 0;
        size_t end = start;

        for (size_t i = start; i < message_buffer.length(); i++) {
          char c = message_buffer[i];

          if (c == '"' && prev_char != '\\') {
            in_string = !in_string;
          } else if (!in_string) {
            if (c == '{') {
              brace_count++;
            } else if (c == '}') {
              brace_count--;
              if (brace_count == 0) {
                // Found complete JSON object
                end = i;
                string complete_message =
                    message_buffer.substr(start, end - start + 1);
                cout << "Complete message found: " << complete_message.length()
                     << " bytes" << endl;
                on_message(complete_message);
                pos = end + 1;
                break;
              }
            }
          }
          prev_char = c;
        }

        // If we didn't find a complete message, break, and wait for more data
        if (brace_count != 0)
          break;
      }

      // Remove processed messages from buffer
      if (pos > 0) {
        message_buffer = message_buffer.substr(pos);
      }

      cout << "Buffer size: " << message_buffer.length() << " bytes" << endl;

      // Prevent buffer from growing too large
      if (message_buffer.length() > 100000) {
        message_buffer.clear();
      }

    } else if (res != CURLE_AGAIN) {
      cout << "WebSocket receive error: " << curl_easy_strerror(res) << endl;
    }
  }

  void on_message(const string &message) {
    Document root;
    root.Parse(message.c_str());

    if (!root.HasParseError()) {
      if (root.HasMember("type")) {
        string type = root["type"].GetString();
        cout << "Message type: " << type << endl;

        if (type == "subscriptions") {
          display_subscription_confirmation(root);
        } else if (type == "ticker") {
          display_ticker_update(root);
        } else if (type == "l2update") {
          display_orderbook_update(root);
        } else if (type == "snapshot") {
          display_orderbook_snapshot(root);
        } else {
          cout << "Unknown message type: " << type << endl;
        }
      } else if (root.HasMember("channel")) {
        string channel = root["channel"].GetString();
        cout << "Message channel: " << channel << endl;

        if (channel == "l2_data") {
          display_orderbook_update(root);
        } else if (channel == "ticker") {
          display_ticker_update(root);
        }
      } else if (root.HasMember("side") && root.HasMember("price_level") &&
                 root.HasMember("new_quantity")) {
        // This is a level2 orderbook update
        display_level2_update(root);
      } else {
        cout << "Unknown message format" << endl;
      }
    } else {
      cout << "JSON parse error in message" << endl;
    }
  }

  void display_subscription_confirmation(const Document &root) {
    cout << "✓ Subscription confirmed for channels: ";
    if (root.HasMember("channels")) {
      const Value &channels = root["channels"];
      for (auto &channel : channels.GetArray()) {
        cout << channel["name"].GetString() << " ";
      }
    }
    cout << endl;
  }

  void display_ticker_update(const Document &root) {
    if (root.HasMember("product_id") && root.HasMember("price")) {
      string product_id = root["product_id"].GetString();
      double price = stod(root["price"].GetString());

      cout << "📈 TICKER: " << product_id << " → $" << fixed << setprecision(2)
           << price;

      if (root.HasMember("best_bid") && root.HasMember("best_ask")) {
        double bid = stod(root["best_bid"].GetString());
        double ask = stod(root["best_ask"].GetString());
        cout << " (Bid: $" << bid << " Ask: $" << ask << ")";
      }
      cout << endl;
    }
  }

  void display_orderbook_snapshot(const Document &root) {
    if (root.HasMember("product_id")) {
      string product_id = root["product_id"].GetString();
      cout << "📊 ORDERBOOK SNAPSHOT: " << product_id << endl;

      if (root.HasMember("bids") && root.HasMember("asks")) {
        const Value &bids = root["bids"];
        const Value &asks = root["asks"];
        cout << "   Bids: " << bids.Size() << " levels, Asks: " << asks.Size()
             << " levels" << endl;
      }
    }
  }

  void display_orderbook_update(const Document &root) {
    if (root.HasMember("product_id") && root.HasMember("changes")) {
      string product_id = root["product_id"].GetString();
      const Value &changes = root["changes"];

      cout << "📋 ORDERBOOK UPDATE: " << product_id << " (" << changes.Size()
           << " changes)" << endl;

      for (auto &change : changes.GetArray()) {
        string side = change[0].GetString();
        double price = stod(change[1].GetString());
        double size = stod(change[2].GetString());

        cout << "   " << (side == "buy" ? "🟢 BUY" : "🔴 SELL") << " $" << fixed
             << setprecision(2) << price << " (" << size << ")" << endl;
      }
    }
  }

  void display_level2_update(const Document &root) {
    if (root.HasMember("side") && root.HasMember("price_level") &&
        root.HasMember("new_quantity")) {
      string side = root["side"].GetString();
      double price = stod(root["price_level"].GetString());
      double quantity = stod(root["new_quantity"].GetString());

      cout << "📊 " << (side == "bid" ? "🟢 BID" : "🔴 ASK") << " $" << fixed
           << setprecision(2) << price << " → " << setprecision(8) << quantity
           << " BTC" << endl;
    }
  }

  void run() {
    connect();

    while (connected) {
      this_thread::sleep_for(chrono::seconds(1));
    }
  }
};
