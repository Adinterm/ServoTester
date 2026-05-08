#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h> //"Websockets" by Markus Sattler
#include <Servo.h>

// WiFi Configuration
const char* ssid = "SSID";
const char* password = "Password";

// Static IP Configuration
IPAddress local_IP(192, 168, 1, 50);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
Servo myServo;

const int servoPin = D1; 
unsigned long lastPing = 0;

// HTML/JS/CSS UI
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Servo Tester Pro</title>
    <style>
        :root { --bg: #121212; --card: #1e1e1e; --accent: #00e676; --error: #ff5252; --text: #e0e0e0; }
        body { font-family: 'Segoe UI', Tahoma, sans-serif; background: var(--bg); color: var(--text); display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; overflow: hidden; }
        .container { width: 90%; max-width: 400px; padding: 25px; background: var(--card); border-radius: 20px; text-align: center; border: 1px solid #333; box-shadow: 0 15px 35px rgba(0,0,0,0.6); }
        
        /* Connection Status */
        #status { font-size: 10px; text-transform: uppercase; letter-spacing: 1px; color: var(--error); margin-bottom: 15px; transition: 0.3s; }
        #status.online { color: var(--accent); }
        
        .mode-toggle { display: flex; gap: 10px; justify-content: center; margin-bottom: 20px; }
        .btn { background: #333; color: white; border: none; padding: 12px; border-radius: 10px; cursor: pointer; transition: 0.2s; flex: 1; font-weight: 600; }
        .btn.active { background: var(--accent); color: #000; }
        
        .val-display { font-size: 4rem; font-weight: bold; color: var(--accent); margin: 10px 0; text-shadow: 0 0 10px rgba(0,230,118,0.3); }
        
        input[type=range] { width: 100%; height: 12px; border-radius: 6px; background: #444; outline: none; -webkit-appearance: none; margin: 30px 0; }
        input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 28px; height: 28px; border-radius: 50%; background: var(--accent); cursor: pointer; border: 4px solid #1e1e1e; }
        
        .presets { display: flex; justify-content: space-between; margin-top: 20px; gap: 8px; }
    </style>
</head>
<body>
    <div class="container">
        <div id="status">&#9679; System Offline</div>
        <h2 style="margin-top:0; font-weight:300; letter-spacing:2px;">SERVO TESTER</h2>
        
        <div class="mode-toggle">
            <button id="btn180" class="btn active" onclick="setMode(false)">180&#176; ANGLE</button>
            <button id="btn360" class="btn" onclick="setMode(true)">360&#176; CONT.</button>
        </div>

        <div class="val-display" id="valOut">90&#176;</div>
        
        <input type="range" id="slider" min="0" max="180" value="90" oninput="sendVal(this.value)">
        
        <div class="presets" id="presetContainer">
            <button class="btn" onclick="sendVal(0)">0&#176;</button>
            <button class="btn" onclick="sendVal(90)">90&#176;</button>
            <button class="btn" onclick="sendVal(180)">180&#176;</button>
        </div>
    </div>

    <script>
        let socket;
        let watchdog;
        let is360 = false;
        const statusDiv = document.getElementById('status');
        const valOut = document.getElementById('valOut');

        function setOffline() {
            statusDiv.innerHTML = "&#9679; System Offline";
            statusDiv.classList.remove('online');
        }

        function connect() {
            socket = new WebSocket('ws://' + window.location.hostname + ':81/');
            
            socket.onopen = () => {
                statusDiv.innerHTML = "&#9679; System Online";
                statusDiv.classList.add('online');
                resetWatchdog();
            };

            socket.onmessage = (event) => {
                if(event.data === "p") resetWatchdog(); 
            };

            socket.onclose = () => {
                setOffline();
                setTimeout(connect, 2000); 
            };
        }

        function resetWatchdog() {
            clearTimeout(watchdog);
            watchdog = setTimeout(() => {
                if(socket) socket.close();
                setOffline();
            }, 3000);
        }

        function setMode(mode) {
            is360 = mode;
            document.getElementById('btn360').classList.toggle('active', is360);
            document.getElementById('btn180').classList.toggle('active', !is360);
            
            const presets = document.getElementById('presetContainer');
            if(is360) {
                presets.innerHTML = `
                    <button class="btn" onclick="sendVal(0)">CCW</button>
                    <button class="btn" onclick="sendVal(90)">STOP</button>
                    <button class="btn" onclick="sendVal(180)">CW</button>`;
            } else {
                presets.innerHTML = `
                    <button class="btn" onclick="sendVal(0)">0&#176;</button>
                    <button class="btn" onclick="sendVal(90)">90&#176;</button>
                    <button class="btn" onclick="sendVal(180)">180&#176;</button>`;
            }
            sendVal(document.getElementById('slider').value);
        }

        function sendVal(v) {
            document.getElementById('slider').value = v;
            if(is360) {
                valOut.innerText = (v == 90) ? "STOP" : (v < 90 ? "CCW" : "CW");
            } else {
                valOut.innerHTML = v + "&#176;";
            }
            if(socket && socket.readyState === WebSocket.OPEN) socket.send(v);
        }

        connect();
    </script>
</body>
</html>
)rawliteral";

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
        int val = atoi((char*)payload);
        if (val >= 0 && val <= 180) {
            myServo.write(val);
            Serial.printf("[%u] Servo moved to: %d\n", num, val);
        }
    }
}

void setup() {
    Serial.begin(115200);

    // FIX: 500us to 2400us allows cheap servos to reach full 180 degree mechanical limits
    myServo.attach(servoPin, 500, 2400); 
    myServo.write(90); 

    WiFi.config(local_IP, gateway, subnet);
    WiFi.begin(ssid, password);
    
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500); 
        Serial.print("."); 
    }
    
    Serial.println("\nIP Address: " + WiFi.localIP().toString());

    server.on("/", []() { 
        server.send(200, "text/html", htmlPage); 
    });
    
    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

void loop() {
    webSocket.loop();
    server.handleClient();

    // Heartbeat: Send 'p' (ping) every 1 second to tell the browser we are still alive
    if (millis() - lastPing > 1000) {
        webSocket.broadcastTXT("p"); 
        lastPing = millis();
    }
}