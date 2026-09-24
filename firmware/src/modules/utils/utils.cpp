#include "modules/utils/utils.h"
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <vector>

#include "modules/state/state.h"

double deg2rad(double deg) {
  return deg * 0.017453292519943295;
}

double wrapAngleDeg(double angle) {
  double wrapped = fmod(angle + 540.0, 360.0);
  if (wrapped < 0) {
    wrapped += 360.0;
  }
  return wrapped - 180.0;
}

double headingErrorDeg(double desired, double current) {
  return wrapAngleDeg(desired - current);
}

String computeSHA256Hex(const String &input) {
  unsigned char hash[32];

  // mbedtls_sha256_ret é deprecated no ESP-IDF >= 5.x
  // Usar mbedtls_sha256 (mesma assinatura, sem sufixo _ret)
#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
  mbedtls_sha256((const unsigned char *)input.c_str(), input.length(), hash, 0);
#else
  mbedtls_sha256_ret((const unsigned char *)input.c_str(), input.length(), hash, 0);
#endif

  String hex;
  hex.reserve(64);
  const char hexChars[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    hex += hexChars[(hash[i] >> 4) & 0x0F];
    hex += hexChars[hash[i] & 0x0F];
  }
  return hex;
}

double nmeaToDecimal(const String &field, char hemisphere) {
  if (field.length() < 4) {
    return 0.0;
  }
  double raw = field.toDouble();
  double degrees = floor(raw / 100.0);
  double minutes = raw - (degrees * 100.0);
  double value = degrees + minutes / 60.0;
  if (hemisphere == 'S' || hemisphere == 'W') {
    value = -value;
  }
  return value;
}

String computeLocalPathHash(const std::vector<String> &lines) {
  // ArduinoJson v7: usar JsonDocument em vez de StaticJsonDocument
  JsonDocument doc;
  String concatenated;
  int count = min((int)lines.size(), 5);
  for (int i = 0; i < count; i++) {
    auto error = deserializeJson(doc, lines[i]);
    if (error) {
      continue;
    }
    double lat = doc["lat"] | 0.0;
    double lon = doc["lon"] | 0.0;
    unsigned long ts = doc["ts"] | 0UL;
    concatenated += String(lat, 6) + "," + String(lon, 6) + "," + String(ts) + ";";
    doc.clear();
  }
  return computeSHA256Hex(concatenated);
}
