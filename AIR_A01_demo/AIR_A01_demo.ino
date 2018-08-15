#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <FS.h>

// モード切り替えピン
const int MODE_PIN = 0; // GPIO0

// Wi-Fi設定保存ファイル
const char* settings = "/wifi_settings.txt";

// サーバモードでのパスワード
const String pass = "12345678";

ESP8266WebServer server(80);

// A01のIPアドレス（固定）
IPAddress airIpAddress(192, 168, 0, 10);

//サーバーモードか？
boolean isServerMode = false;

//LEDのピン番号(0だと未使用)
int ledPin = 4;

//トリガーボタン(0だと未使用)
int trigPin = 16;

/**
 * LED関連
 */
void ledSetup() {
  if (ledPin != 0) {
    pinMode(ledPin , OUTPUT);
  }
}

void ledOn() {
  if (ledPin != 0) {
    digitalWrite(ledPin,HIGH);
  }
}

void ledOff() {
  if (ledPin != 0) {
    digitalWrite(ledPin,LOW);
  }
}

/**
 * Button関連
 */
void trigSetup() {
  if (trigPin != 0) {
    pinMode(trigPin , INPUT);
  }
}

boolean getTrig() {
  boolean retval = false;
  if (trigPin != 0) {
    if (digitalRead(trigPin) == LOW) {     //スイッチの状態を調べる
      Serial.println("SW LOW");
      retval =  true;
    } else {
      Serial.println("SW HIGH");
    }
  } else {
    retval = true;
  }

  return retval;
}

/**
 * WiFi設定
 */
void handleRootGet() {
  String html = "";
  html += "<h1>WiFi Settings</h1>";
  html += "<form method='post'>";
  html += "  <input type='text' name='ssid' placeholder='ssid'><br>";
  html += "  <input type='text' name='pass' placeholder='pass'><br>";
  html += "  <input type='submit'><br>";
  html += "</form>";
  server.send(200, "text/html", html);
}

void handleRootPost() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  File f = SPIFFS.open(settings, "w");
  f.println(ssid);
  f.println(pass);
  f.close();

  String html = "";
  html += "<h1>WiFi Settings</h1>";
  html += ssid + "<br>";
  html += pass + "<br>";
  server.send(200, "text/html", html);
}

/**
 * 初期化(クライアントモード)
 */
void setup_client() {
  boolean ledStatus = false;
  File f = SPIFFS.open(settings, "r");
  String ssid = f.readStringUntil('\n');
  String pass = f.readStringUntil('\n');
  f.close();

  ssid.trim();
  pass.trim();

  Serial.println("SSID: " + ssid);
  Serial.println("PASS: " + pass);

  WiFi.begin(ssid.c_str(), pass.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (ledStatus) {
      ledStatus = false;
      ledOff();
    } else  {
      ledStatus = true;
      ledOn();
    }
  }

  ledOff();
  
  Serial.println("");

  Serial.println("WiFi connected");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // 撮影モードへの切り替えをリクエスト
  if (sendRequest("/switch_cameramode.cgi?mode=rec&lvqty=0320x0240") == 200) {
    // 撮影モードへの切り替えに成功したらライブビューを開始
    // 実際にはライブビューは使用しないが、開始せずに撮影しようとするとエラーになるため
    Serial.println("Rec mode");
    sendRequest("/exec_takemisc.cgi?com=startliveview&port=5555");
  } else {
    Serial.println("Failed to switch to rec mode");
  }
  
  isServerMode = false;
}

/**
 * 初期化(サーバモード)
 */
void setup_server() {
  byte mac[6];
  WiFi.macAddress(mac);
  String ssid = "";
  for (int i = 0; i < 6; i++) {
    ssid += String(mac[i], HEX);
  }
  Serial.println("SSID: " + ssid);
  Serial.println("PASS: " + pass);

  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAP(ssid.c_str(), pass.c_str());

  server.on("/", HTTP_GET, handleRootGet);
  server.on("/", HTTP_POST, handleRootPost);
  server.begin();
  Serial.println("HTTP server started.");

  isServerMode = true;
}


void setup() {
  Serial.begin(115200);
  Serial.println();

  ledSetup();
  ledOn();
  
  // 1秒以内にMODEを切り替える
  //  0 : Server
  //  1 : Client
  delay(1000);

  ledOff();
    
  // ファイルシステム初期化
  SPIFFS.begin();

  pinMode(MODE_PIN, INPUT);
  if (digitalRead(MODE_PIN) == 0) {
    // サーバモード初期化
    setup_server();
  } else {
    // クライアントモード初期化
    setup_client();
  }
}

void loop() {
  if (isServerMode) {
    //サーバーモード
    server.handleClient();
  } else {
    if (getTrig()) {
      ledOn();
      // 静止画撮影開始
      if (sendRequest("/exec_takemotion.cgi?com=newstarttake") == 200) {
        Serial.println("Started taking");
  
        // 撮影開始に成功したら静止画撮影停止
        sendRequest("/exec_takemotion.cgi?com=newstoptake");
        Serial.println("Stopped taking");
      } else {
        Serial.println("Failed to start taking");
      }
      
      delay(5000);
      ledOff();
    } else {
      delay(100);
    }
  }
}

// HTTPでリクエストを送信して結果を返す
int sendRequest(String command) {
  int result = -1;

  Serial.println("Connecting to the AIR\n");

  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(airIpAddress, httpPort)) {
    Serial.println("Connection failed");
    return result;
  }

  Serial.print("Command: ");
  Serial.println(command);

  client.println(String("GET ") + command + " HTTP/1.1");
  client.println("Host:192.168.0.10");
  client.println("User-Agent:OlympusCameraKit");
  client.println();

  // 暫定的に一定時間待つようにしている
  // ここはタイムアウト付きで一定の返答があるまで待つように変更する予定
  delay(100);

  // クライアントにメッセージが届いていればそれを読み取って処理する
  while (client.available()) {
    String line = client.readStringUntil('\r');

    if (line.startsWith("HTTP/1.1")) {
      //           11111
      // 012345678901234
      // HTTP/1.1 200 OK
      result = line.substring(9, 12).toInt();
    }

    Serial.print(line);
  }
  Serial.println();

  if (client.connected()) {
    client.stop();
  }

  return result;
}
