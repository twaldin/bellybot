// stupid ahh deprecated rapidjson
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "coinbase_websocket.h"
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

using namespace rapidjson;

string get_timestamp() { return to_string(time(nullptr)); }

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
  bool snapshot_loaded = false;
  int fragment_count = 0;

  map<double, double, greater<double>> bids;
  map<double, double> asks;
  TickerData latest_ticker;

  string current_price = "Loading...";
  string current_bid = "Loading...";
  string current_ask = "Loading...";

public:
  coinbase_ws_client() {
    curl = curl_easy_init();
    if (!curl) {
      throw runtime_error("Failed to initialize curl");
    }

    // No API credentials needed for public market data
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

        // Subscribe to each channel individually to avoid auth issues
        subscribe_to_channels({"BTC-USD"}, {"level2"});
        subscribe_to_channels({"BTC-USD"}, {"ticker"});

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

  void subscribe_to_channels(const vector<string> &product_ids,
                             const vector<string> &channels) {
    if (!connected) {
      cout << "Not connected to WebSocket" << endl;
      return;
    }

    Document subscription;
    subscription.SetObject();
    Document::AllocatorType &allocator = subscription.GetAllocator();

    subscription.AddMember("type", Value("subscribe", allocator), allocator);

    Value product_ids_array(kArrayType);
    for (const string &product_id : product_ids) {
      product_ids_array.PushBack(Value(product_id.c_str(), allocator),
                                 allocator);
    }
    subscription.AddMember("product_ids", product_ids_array, allocator);

    // For single channel, use "channel" instead of "channels" array
    if (channels.size() == 1) {
      subscription.AddMember("channel", Value(channels[0].c_str(), allocator),
                             allocator);
    } else {
      Value channels_array(kArrayType);
      for (const string &channel : channels) {
        channels_array.PushBack(Value(channel.c_str(), allocator), allocator);
      }
      subscription.AddMember("channels", channels_array, allocator);
    }

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    subscription.Accept(writer);

    string message = buffer.GetString();
    cout << "Sending subscription: " << message << endl;

    size_t sent;
    CURLcode res = curl_ws_send(curl, message.c_str(), message.length(), &sent,
                                0, CURLWS_TEXT);

    if (res != CURLE_OK) {
      cout << "Send error: " << curl_easy_strerror(res) << endl;
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

      // Add fragment to buffer and show loading progress
      message_buffer += fragment;
      fragment_count++;

      if (!snapshot_loaded) {
        show_loading_dashboard(fragment.length(), message_buffer.length());
      }
      // Handle very large snapshots by setting a reasonable limit
      if (message_buffer.length() >
          1000000000) { // 1GB limit for maximum size snapshots
        cout << "Buffer too large (" << message_buffer.length()
             << " bytes), trying to parse anyway..." << endl;

        // Try to parse the entire buffer as JSON
        Document large_doc;
        large_doc.Parse(message_buffer.c_str());
        if (!large_doc.HasParseError()) {
          cout << "Successfully parsed large JSON message!" << endl;
          on_message(message_buffer);
          message_buffer.clear();
          return;
        } else {
          cout << "Failed to parse large buffer, clearing..." << endl;
          message_buffer.clear();
          return;
        }
      }

      // Try to parse current buffer as complete JSON
      Document buffer_doc;
      buffer_doc.Parse(message_buffer.c_str());
      if (!buffer_doc.HasParseError()) {
        cout << "Complete JSON message found! Length: "
             << message_buffer.length() << " bytes" << endl;
        on_message(message_buffer);
        message_buffer.clear();
      }

      // Prevent buffer from growing too large
      if (message_buffer.length() > 1000000000) {
        message_buffer.clear();
      }

    } else if (res != CURLE_AGAIN) {
      cout << "WebSocket receive error: " << curl_easy_strerror(res) << endl;
    }
  }

  void on_message(const string &message) {
    Document root;
    root.Parse(message.c_str());

    if (root.HasParseError()) {
      cout << "❌ JSON parse error!" << endl;
      return;
    }

    if (root.HasMember("channel")) {
      string channel = root["channel"].GetString();

      if (channel == "l2_data") {
        handle_l2_data(root);
      } else if (channel == "ticker") {
        if (root.HasMember("events")) {
          const Value &events = root["events"];
          for (auto &event : events.GetArray()) {
            handle_ticker_data(event);
          }
        }
      } else if (channel == "subscriptions") {
        display_subscription_confirmation(root);
      }
    } else if (root.HasMember("type")) {
      string type = root["type"].GetString();

      if (type == "subscriptions") {
        display_subscription_confirmation(root);
      } else if (type == "error") {
        if (root.HasMember("message")) {
          cout << "❌ ERROR: " << root["message"].GetString() << endl;
        }
      }
    }
  }
  void handle_l2_data(const Document &root) {
    if (!root.HasMember("events"))
      return;

    const Value &events = root["events"];
    for (auto &event : events.GetArray()) {
      handle_l2_event(event);
    }
  }

  void handle_l2_event(const Value &event) {
    if (!event.HasMember("type") || !event.HasMember("product_id"))
      return;

    string type = event["type"].GetString();
    string product_id = event["product_id"].GetString();

    if (type == "snapshot") {
      handle_l2_snapshot(event, product_id);
    } else if (type == "update") {
      handle_l2_update(event, product_id);
    }
  }

  void clear_screen() { cout << "\033[2J\033[H" << flush; }

  void show_loading_dashboard(size_t fragment_size, size_t buffer_size) {
    clear_screen();
    cout << "🚀 BellyBot - Coinbase Advanced Trade API Monitor\n" << endl;
    cout << "📡 Connecting to Coinbase Advanced Trade WebSocket..." << endl;
    cout << "🔄 Loading orderbook snapshot...\n" << endl;

    cout << "📊 SNAPSHOT LOADING PROGRESS:" << endl;
    cout << "┌─────────────────────────────────────────────────┐" << endl;

    cout << "│ Fragment #" << setw(4) << fragment_count << " received"
         << setw(28) << "│" << endl;
    cout << "│ Fragment Size: " << setw(8) << fragment_size << " bytes"
         << setw(22) << "│" << endl;
    cout << "│ Buffer Size:   " << setw(8) << buffer_size << " bytes"
         << setw(22) << "│" << endl;
    cout << "│ Progress:      " << setw(6) << fixed << setprecision(1)
         << (buffer_size / 1024.0) << " KB" << setw(27) << "│" << endl;
    cout << "└─────────────────────────────────────────────────┘" << endl;

    // Progress bar
    int progress_width = 40;
    int estimated_size = 5000000; // 5MB estimated
    int progress = min(progress_width,
                       (int)((buffer_size * progress_width) / estimated_size));

    cout << "\n[";
    for (int i = 0; i < progress_width; i++) {
      if (i < progress) {
        cout << "█";
      } else {
        cout << "░";
      }
    }
    cout << "] " << (progress * 100 / progress_width) << "%" << endl;

    cout << "\n⏳ Please wait while the complete orderbook loads..." << endl;
  }

  void handle_l2_snapshot(const Value &event, const string &product_id) {
    snapshot_loaded = true; // Mark snapshot as loaded
    clear_screen();
    cout << "🚀 BellyBot - Coinbase Advanced Trade API Monitor\n" << endl;
    cout << "✅ ORDERBOOK SNAPSHOT LOADED: " << product_id << endl;
    cout << "📊 Total fragments received: " << fragment_count << endl;
    cout << "💾 Final buffer size: " << fixed << setprecision(2)
         << (message_buffer.length() / 1024.0 / 1024.0) << " MB\n"
         << endl;

    if (!event.HasMember("updates"))
      return;

    const Value &updates = event["updates"];
    for (auto &update : updates.GetArray()) {
      if (!update.HasMember("side") || !update.HasMember("price_level") ||
          !update.HasMember("new_quantity")) {
        continue;
      }

      string side = update["side"].GetString();
      double price = stod(update["price_level"].GetString());
      double quantity = stod(update["new_quantity"].GetString());

      if (side == "bid") {
        bids[price] = quantity;
      } else if (side == "offer") {
        asks[price] = quantity;
      }
    }

    cout << "📈 Bids: " << bids.size() << " levels | 📉 Asks: " << asks.size()
         << " levels" << endl;
    cout << "🔄 Real-time updates active...\n" << endl;
  }

  void handle_l2_update(const Value &event, const string &product_id) {
    if (!event.HasMember("updates"))
      return;

    const Value &updates = event["updates"];

    for (auto &update : updates.GetArray()) {
      add_update_to_dashboard(update);
    }

    // Refresh dashboard after processing all updates
    show_live_dashboard();
  }

  void add_update_to_dashboard(const Value &update) {
    if (!update.HasMember("side") || !update.HasMember("price_level") ||
        !update.HasMember("new_quantity")) {
      return;
    }

    string side = update["side"].GetString();
    double price = stod(update["price_level"].GetString());
    double quantity = stod(update["new_quantity"].GetString());

    if (side == "bid") {
      if (quantity == 0) {
        bids.erase(price);
      } else {
        bids[price] = quantity;
      }
    } else if (side == "offer") {
      if (quantity == 0) {
        asks.erase(price);
      } else {
        asks[price] = quantity;
      }
    }
  }

  void show_live_dashboard() {
    clear_screen();
    cout << "🚀 BellyBot - Coinbase Advanced Trade API Monitor\n" << endl;
    cout << "💰 BTC-USD: " << current_price << " | Bid: " << current_bid
         << " | Ask: " << current_ask << "\n"
         << endl;

    cout << "┌─────────────── BIDS 🟢 ───────────────┬─────────────── ASKS 🔴 "
            "───────────────┐"
         << endl;

    auto bid_it = bids.begin();
    auto ask_it = asks.begin();

    for (int i = 0; i < 10; ++i) {
      string bid_line;
      if (bid_it != bids.end()) {
        ostringstream bid_str;
        bid_str << "$" << fixed << setprecision(2) << bid_it->first << " → "
                << setprecision(12) << bid_it->second << " BTC";
        bid_line = bid_str.str();
        ++bid_it;
      } else {
        bid_line = "";
      }

      string ask_line;
      if (ask_it != asks.end()) {
        ostringstream ask_str;
        ask_str << "$" << fixed << setprecision(2) << ask_it->first << " → "
                << setprecision(12) << ask_it->second << " BTC";
        ask_line = ask_str.str();
        ++ask_it;
      } else {
        ask_line = "";
      }

      cout << "│ " << setw(39) << left << bid_line << " │ " << setw(39) << left
           << ask_line << " │" << endl;
    }

    cout << "└───────────────────────────────────────┴─────────────────────────"
            "──────────────┘"
         << endl;
    cout << "\n🔄 Live updates | Press Ctrl+C to exit" << endl;
  }

  void handle_ticker_data(const Value &event) {
    if (!event.HasMember("tickers"))
      return;

    const Value &tickers = event["tickers"];
    for (auto &ticker : tickers.GetArray()) {
      display_ticker_entry(ticker);
    }
  }

  void display_ticker_entry(const Value &ticker) {
    if (!ticker.HasMember("product_id") || !ticker.HasMember("price"))
      return;

    double price = stod(ticker["price"].GetString());

    ostringstream price_str;
    price_str << "$" << fixed << setprecision(2) << price;
    current_price = price_str.str();

    if (ticker.HasMember("best_bid") && ticker.HasMember("best_ask")) {
      double bid = stod(ticker["best_bid"].GetString());
      double ask = stod(ticker["best_ask"].GetString());

      ostringstream bid_str, ask_str;
      bid_str << "$" << fixed << setprecision(2) << bid;
      ask_str << "$" << fixed << setprecision(2) << ask;
      current_bid = bid_str.str();
      current_ask = ask_str.str();
    }

    // Update dashboard with new ticker info
    show_live_dashboard();
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

  void run() {
    connect();

    while (connected) {
      this_thread::sleep_for(chrono::seconds(1));
    }
  }
};
