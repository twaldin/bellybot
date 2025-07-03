#include "kraken_websocket.cpp"

int main() {

  curl_global_init(CURL_GLOBAL_DEFAULT);

  try {
    KrakenWebSocketClient client;
    client.run();
  } catch (const exception &e) {
    cerr << "Error: " << e.what() << endl;
  }

  curl_global_cleanup();
  return 0;
  return 0;
}
