#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>

const char* apSSID = "ESP_DNS_HTTP";
const char* apPassword = "esp8266proxy";

const char* staSSID = "<WIFI_SSID>";
const char* staPassword = "WIFI_PASSWORD";


WiFiUDP dnsUDP;
WiFiServer httpProxy(8080);
ESP8266WebServer webServer(80);

IPAddress dnsServerIP(8, 8, 8, 8);
String logBuffer = "";

void setup() {
  Serial.begin(115200);

  // Connect to your home router
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(staSSID, staPassword);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to router IP: " + WiFi.localIP().toString());

  // Start AP for clients
  WiFi.softAP(apSSID, apPassword);
  Serial.println("Access Point Started at IP: " + WiFi.softAPIP().toString());

  // Start services
  dnsUDP.begin(53);
  httpProxy.begin();
  webServer.on("/", handleRoot);
  webServer.on("/logs", handleLogs);
  webServer.begin();
  Serial.println("DNS Interceptor + HTTP Proxy + Log Web Server started");
}


void loop() {
  handleDNS();
  handleHTTPProxy();
  webServer.handleClient();
}

void handleDNS() {
  int packetSize = dnsUDP.parsePacket();
  if (packetSize) {
    byte packet[512];
    dnsUDP.read(packet, packetSize);
    IPAddress clientIP = dnsUDP.remoteIP();
    uint16_t clientPort = dnsUDP.remotePort();

    String domain = parseDomain(packet);
    String logEntry = "[DNS] " + clientIP.toString() + " -> " + domain + "\n";
    Serial.print(logEntry);
    appendToLog(logEntry);

    WiFiUDP forwarder;
    forwarder.beginPacket(dnsServerIP, 53);
    forwarder.write(packet, packetSize);
    forwarder.endPacket();

    delay(50);
    int len = forwarder.parsePacket();
    if (len) {
      byte response[512];
      forwarder.read(response, len);
      dnsUDP.beginPacket(clientIP, clientPort);
      dnsUDP.write(response, len);
      dnsUDP.endPacket();
    }
  }
}

String parseDomain(byte* buffer) {
  String domain = "";
  int pos = 12;
  while (buffer[pos] != 0) {
    int len = buffer[pos++];
    for (int i = 0; i < len; i++) {
      domain += (char)buffer[pos++];
    }
    domain += ".";
  }
  domain.remove(domain.length() - 1);
  return domain;
}

void handleHTTPProxy() {
  WiFiClient client = httpProxy.available();
  if (client) {
    String requestLine = client.readStringUntil('\n');
    if (!requestLine.startsWith("GET http://") && !requestLine.startsWith("POST http://")) {
      client.println("HTTP/1.1 400 Bad Request\r\n\r\nOnly HTTP requests supported.");
      client.stop();
      return;
    }

    int hostStart = requestLine.indexOf("http://") + 7;
    int pathStart = requestLine.indexOf("/", hostStart);
    String host = requestLine.substring(hostStart, pathStart);
    String path = requestLine.substring(pathStart);

    String logEntry = "[HTTP] -> " + host + path + "\n";
    Serial.print(logEntry);
    appendToLog(logEntry);

    WiFiClient remote;
    if (remote.connect(host.c_str(), 80)) {
      remote.print(requestLine.substring(0, pathStart - hostStart));
      remote.print(path + " HTTP/1.0\r\n");
      remote.print("Host: " + host + "\r\n");
      remote.print("Connection: close\r\n\r\n");

      while (remote.connected()) {
        while (remote.available()) {
          char c = remote.read();
          client.write(c);
        }
      }
      remote.stop();
    } else {
      client.println("HTTP/1.1 502 Bad Gateway\r\n\r\nUnable to connect to host.");
    }

    client.stop();
  }
}

void appendToLog(String entry) {
  logBuffer += entry;
  if (logBuffer.length() > 3000) {  // Limit to ~3KB
    int firstNewline = logBuffer.indexOf('\n');
    logBuffer = logBuffer.substring(firstNewline + 1);
  }
}

void handleRoot() {
  webServer.send(200, "text/html", R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>ESP Log Viewer</title>
      <meta charset="UTF-8">
      <style>
        body { font-family: monospace; background: #111; color: #0f0; padding: 1em; }
        pre { white-space: pre-wrap; word-wrap: break-word; }
      </style>
    </head>
    <body>
      <h2>ESP8266 Real-Time Logs</h2>
      <pre id="logArea">Loading...</pre>
      <script>
        setInterval(() => {
          fetch('/logs')
            .then(r => r.text())
            .then(t => document.getElementById('logArea').textContent = t);
        }, 1000);
      </script>
    </body>
    </html>
  )rawliteral");
}

void handleLogs() {
  webServer.send(200, "text/plain", logBuffer);
}
