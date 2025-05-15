#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP32Servo.h>
#include <Arduino_JSON.h>

// Pin assignments
const int buttonPin = 4;   // Button input pin
const int servoPin = 35;   // Servo control pin

// Button state variables
int buttonState = 0;      // Current state of the button
int lastButtonState = 0;  // Previous state of the button

// Servo configuration
Servo myServo;
int servoPos = 0;         // Current position of servo

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// JSON Variable to hold sensor readings
JSONVar readings;

// Timer variables for periodic updates
unsigned long lastTime = 0;
unsigned long timerDelay = 100;  // Send updates every 100ms

// HTML webpage
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Button to Servo Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
    body { font-family: Arial; text-align: center; margin: 0px auto; padding: 15px; }
    .button {
        padding: 15px 50px;
        font-size: 24px;
        background-color: #4CAF50;
        color: white;
        border: none;
        border-radius: 5px;
        cursor: pointer;
        margin: 10px;
    }
    .slider {
        width: 300px;
        margin: 20px auto;
    }
    .status {
        font-size: 18px;
        margin: 20px 0;
    }
    </style>
</head>
<body>
    <h1>ESP32 Button to Servo Control</h1>
    
    <div class="status">
    Button State: <span id="buttonState">Unknown</span>
    </div>
    
    <div>
    <input type="range" min="0" max="180" value="90" class="slider" id="servoSlider">
    <p>Servo Position: <span id="servoPos">90</span>°</p>
    </div>
    
    <button class="button" id="btn0deg">0°</button>
    <button class="button" id="btn90deg">90°</button>
    <button class="button" id="btn180deg">180°</button>

    <script>
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket;
    
    // Connect to WebSocket server
    window.addEventListener('load', onLoad);
    
    function onLoad(event) {
        initWebSocket();
        
        // Set up event listeners for buttons
        document.getElementById('btn0deg').addEventListener('click', () => {
        sendServoPosition(0);
        });
        
        document.getElementById('btn90deg').addEventListener('click', () => {
        sendServoPosition(90);
        });
        
        document.getElementById('btn180deg').addEventListener('click', () => {
        sendServoPosition(180);
        });
        
        // Set up slider event listener
        document.getElementById('servoSlider').addEventListener('input', function() {
        let position = this.value;
        document.getElementById('servoPos').textContent = position;
        sendServoPosition(parseInt(position));
        });
    }
    
    function initWebSocket() {
        console.log('Trying to open a WebSocket connection...');
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
    }
    
    function onOpen(event) {
        console.log('Connection opened');
    }
    
    function onClose(event) {
        console.log('Connection closed');
        setTimeout(initWebSocket, 2000);
    }
    
    function onMessage(event) {
        console.log('Received: ' + event.data);
        var data = JSON.parse(event.data);
        
        if (data.hasOwnProperty('buttonState')) {
        document.getElementById('buttonState').textContent = 
            data.buttonState == "1" ? "Pressed" : "Released";
        }
        
        if (data.hasOwnProperty('servoPos')) {
        document.getElementById('servoPos').textContent = data.servoPos;
        document.getElementById('servoSlider').value = data.servoPos;
        }
    }
    
    function sendServoPosition(position) {
        var data = {
        action: 'setServo',
        position: position
        };
        websocket.send(JSON.stringify(data));
    }
    </script>
</body>
</html>
)rawliteral";

// Initialize WiFi
void initWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
    }
    Serial.println("");
    Serial.print("Connected to WiFi! IP address: ");
    Serial.println(WiFi.localIP());
}

// Send current button state and servo position to connected WebSocket clients
void notifyClients() {
    JSONVar data;
    data["buttonState"] = String(buttonState);
    data["servoPos"] = String(servoPos);
    String jsonString = JSON.stringify(data);
    ws.textAll(jsonString);
}

// Handle incoming WebSocket messages
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    // Null-terminate the data to treat it as a string
    data[len] = 0;
    
    // Parse JSON message
    String message = String((char*)data);
    JSONVar jsonObject = JSON.parse(message);
    
    // Check if JSON parsing was successful
    if (JSON.typeof(jsonObject) == "undefined") {
        Serial.println("JSON parsing failed!");
        return;
    }
    
    // Check if it's a servo control message
    if (jsonObject.hasOwnProperty("action") && jsonObject.hasOwnProperty("position")) {
        if (String((const char*)jsonObject["action"]) == "setServo") {
        servoPos = (int)jsonObject["position"];
        
        // Move the servo to the specified position
        myServo.write(servoPos);
        Serial.print("Moving servo to: ");
        Serial.println(servoPos);
        
        // Notify all clients of the new position
        notifyClients();
        }
    }
    }
}

// WebSocket event handler
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        // Send current state to newly connected client
        notifyClients();
        break;
    case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
    case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
        break;
    }
}

// Initialize WebSocket server
void initWebSocket() {
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}

// HTML template processor (not used in this example but kept for expandability)
String processor(const String& var) {
    return String();
}

void setup() {
    // Serial port for debugging purposes
    Serial.begin(115200);
    
    // Initialize button pin as input
    pinMode(buttonPin, INPUT_PULLUP);  // Using internal pull-up resistor
    
    // Initialize servo
    ESP32PWM::allocateTimer(0);  // Allocate timer for servo
    myServo.setPeriodHertz(50);  // Standard 50Hz servo
    myServo.attach(servoPin, 500, 2400);  // Attach servo to pin with min/max pulse values
    
    // Initialize WiFi and WebSocket
    initWiFi();
    initWebSocket();
    
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
    });
    
    // Start server
    server.begin();
    Serial.println("HTTP server started");
    
    // Set initial servo position
    myServo.write(90);
    servoPos = 90;
}

void loop() {
    // Read button state
    buttonState = digitalRead(buttonPin);
    
    // If button state changed, notify clients
    if (buttonState != lastButtonState) {
    Serial.print("Button state changed to: ");
    Serial.println(buttonState == HIGH ? "Released" : "Pressed");
    
    // If button is pressed (LOW with pull-up), move servo to 180, otherwise to 0
    if (buttonState == LOW) {
        servoPos = 180;
        myServo.write(servoPos);
    } else {
        servoPos = 0;
        myServo.write(servoPos);
    }
    
    notifyClients();
    lastButtonState = buttonState;
    }
    
    // Update clients periodically in case any other state changes
    if ((millis() - lastTime) > timerDelay) {
    notifyClients();
    lastTime = millis();
    }
    
    ws.cleanupClients();
}