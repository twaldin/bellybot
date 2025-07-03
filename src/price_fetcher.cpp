#include "../inc/price_fetcher.h"

using json = nlohmann::json;

size_t WriteCallback(void *contents, size_t size, size_t nmemb, string *userp) {
  userp->append((char *)contents, size * nmemb);
  return size * nmemb;
}

class StockPriceFetcher {
private:
  string api_key;

public:
  StockPriceFetcher(const string &key) : api_key(key) {}

  // Get stock price using Alpha Vantage API
  double getStockPrice(const string &symbol) {
    CURL *curl;
    CURLcode res;
    string response_data;

    curl = curl_easy_init();
    if (curl) {
      // Build API URL
      string url =
          "https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol=" +
          symbol + "&apikey=" + api_key;

      // Set curl options
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

      // Perform the request
      res = curl_easy_perform(curl);
      curl_easy_cleanup(curl);

      if (res == CURLE_OK) {
        return parsePrice(response_data);
      } else {
        cerr << "CURL error: " << curl_easy_strerror(res) << endl;
        return -1.0;
      }
    }
    return -1.0;
  }

private:
  // Parse Alpha Vantage JSON response
  double parsePrice(const string &json_data) {
    try {
      json j = json::parse(json_data);

      // Check if API call was successful
      if (j.contains("Error Message")) {
        cerr << "API Error: " << j["Error Message"] << endl;
        return -1.0;
      }

      // Extract price from Global Quote
      if (j.contains("Global Quote")) {
        string price_str = j["Global Quote"]["05. price"];
        return stod(price_str);
      }

    } catch (const exception &e) {
      cerr << "JSON parsing error: " << e.what() << endl;
    }
    return -1.0;
  }
};
