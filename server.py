from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime, timezone
import json

class MonitoringHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/api/measurements":
            self.send_error(404)
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096:
                self.send_error(400, "Invalid body size")
                return

            data = json.loads(self.rfile.read(length))
            temperature = data["temperature_c"]
            humidity = data["humidity_percent"]
            device_id = data.get("device_id", "esp32-01")

            if not isinstance(temperature, (int, float)):
                raise ValueError("Invalid temperature")
            if not isinstance(humidity, (int, float)):
                raise ValueError("Invalid humidity")
            if not -10 <= temperature <= 60:
                raise ValueError("Temperature out of range")
            if not 0 <= humidity <= 100:
                raise ValueError("Humidity out of range")

            record = {
                "received_at": datetime.now(timezone.utc).isoformat(),
                "device_id": str(device_id),
                "temperature_c": temperature,
                "humidity_percent": humidity,
            }

            print(json.dumps(record), flush=True)
            body = b'{"status":"ok"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        except (ValueError, KeyError, TypeError, json.JSONDecodeError):
            self.send_error(400, "Invalid measurement")

    def log_message(self, fmt, *args):
        print(f"{self.client_address[0]} - {fmt % args}", flush=True)

server = ThreadingHTTPServer(("0.0.0.0", 8080), MonitoringHandler)
print("Monitoring server listening on port 8080", flush=True)
server.serve_forever()