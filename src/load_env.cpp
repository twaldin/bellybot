#import "load_env.h"

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
