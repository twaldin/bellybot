#include "price_fetcher.cpp"

void loadEnvFile(const string &filename = ".env") {
  ifstream file(filename);
  string line;

  while (getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue; // Skip empty lines and comments

    size_t pos = line.find('=');
    if (pos != string::npos) {
      string key = line.substr(0, pos);
      string value = line.substr(pos + 1);

      // Remove quotes if present
      if (value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.length() - 2);
      }

      setenv(key.c_str(), value.c_str(), 1);
    }
  }
}

int main() {
  loadEnvFile();
  curl_global_init(CURL_GLOBAL_DEFAULT);

  const char *api_key_env = getenv("ALPHA_VANTAGE_API_KEY");
  if (!api_key_env) {
    cerr << "Error: ALPHA_VANTAGE_API_KEY environment variable not set!"
         << endl;
    cerr << "Please add it to your .env file" << endl;
    return 1;
  }

  string api_key = api_key_env;
  StockPriceFetcher fetcher(api_key);

  string symbol = "NVDA";
  double price = fetcher.getStockPrice(symbol);

  if (price > 0) {
    cout << "Current price of " << symbol << ": $" << price << endl;
  } else {
    cout << "Failed to get price for " << symbol << endl;
  }

  curl_global_cleanup();

  return 0;
}
