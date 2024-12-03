#include <COP5611_ProjectV2_inferencing.h>
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <base64.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_http_server.h"
#include "img_converters.h"
#include "image_util.h"
#include "esp_camera.h"

#define CAMERA_MODEL_AI_THINKER 

#include "camera_pins.h"

#define PART_BOUNDARY "123456789000000000000987654321"

String globalPredictionOutput;
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;


const char* ssid = "David's Galaxy S21 5G";
const char* password = "";

dl_matrix3du_t *resized_matrix = NULL;
size_t out_len = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
ei_impulse_result_t result = {0};
void setupOTA() {
    ArduinoOTA.setHostname("esp32-cam"); // Set the hostname

    ArduinoOTA
        .onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH) {
                type = "sketch";
            } else { // U_SPIFFS
                type = "filesystem";
            }
            Serial.println("Start updating " + type);
        })
        .onEnd([]() {
            Serial.println("\nEnd");
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        })
        .onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR) Serial.println("End Failed");
        });

    ArduinoOTA.begin();
}
void setup() {
  Serial.begin(115200);
      setupWiFi(); // Initialize WiFi
    setupOTA(); // Initialize OTA
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_XGA; 


  config.jpeg_quality = 80;
  config.fb_count = 1;

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();

  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); 
    s->set_brightness(s, 1); 
    s->set_saturation(s, 0);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

int raw_feature_get_data(size_t offset, size_t out_len, float *signal_ptr)
{
  size_t pixel_ix = offset * 3;
  size_t bytes_left = out_len;
  size_t out_ptr_ix = 0;

  // read byte for byte
  while (bytes_left != 0) {
    // grab the values and convert to r/g/b
    uint8_t r, g, b;
    r = resized_matrix->item[pixel_ix];
    g = resized_matrix->item[pixel_ix + 1];
    b = resized_matrix->item[pixel_ix + 2];

    // then convert to out_ptr format
    float pixel_f = (r << 16) + (g << 8) + b;
    signal_ptr[out_ptr_ix] = pixel_f;

    // and go to the next pixel
    out_ptr_ix++;
    pixel_ix += 3;
    bytes_left--;
  }
  return 0;
}

void classify()
{
  Serial.println("Getting signal...");
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_WIDTH;
  signal.get_data = &raw_feature_get_data;

  Serial.println("Run classifier...");
  // Feed signal to the classifier

  // Run the classifier
  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false /* debug */);
  if (res != EI_IMPULSE_OK) {
    Serial.printf("ERR: Failed to run classifier (%d)\n", res);
    return;
  }

  // print the predictions
  String predictionOutput;
  predictionOutput.reserve(1024); // Reserve a large enough buffer

  predictionOutput += "Predictions (DSP: " + String(result.timing.dsp) + " ms., Classification: " + String(result.timing.classification) + " ms., Anomaly: " + String(result.timing.anomaly) + " ms.): \n";

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
  bool objectFound = false;
  for (size_t ix = 0; ix < result.bounding_boxes_count; ix++) {
    auto bb = result.bounding_boxes[ix];
    if (bb.value > 0.5) {
      String objectInfo = "    " + String(bb.label) + " (" + String(bb.value) + ") [ x: " + String(bb.x) + ", y: " + String(bb.y) + ", width: " + String(bb.width) + ", height: " + String(bb.height) + " ]\n";
      predictionOutput += objectInfo;
      Serial.print(objectInfo); // Print object info to the console
      objectFound = true;
    }
  }

  if (!objectFound) {
    String noObjectsInfo = "    No objects found\n";
    predictionOutput += noObjectsInfo;
    Serial.print(noObjectsInfo); // Print "No objects found" to the console
  }
#else
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    String classificationInfo = "    " + String(result.classification[ix].label) + ": " + String(result.classification[ix].value) + "\n";
    predictionOutput += classificationInfo;
    Serial.print(classificationInfo); // Print classification info to the console
  }

#if EI_CLASSIFIER_HAS_ANOMALY == 1
  String anomalyInfo = "    Anomaly score: " + String(result.anomaly) + "\n";
  predictionOutput += anomalyInfo;
  Serial.print(anomalyInfo); // Print anomaly score to the console
#endif
#endif

  // Store the prediction output in the global variable
  globalPredictionOutput = predictionOutput;
}

static esp_err_t capture_handler(httpd_req_t *req) {

  esp_err_t res = ESP_OK;
  camera_fb_t * fb = NULL;

  Serial.println("Capture image");
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // --- Convert frame to RGB888  ---

  Serial.println("Converting to RGB888...");
  // Allocate rgb888_matrix buffer
  dl_matrix3du_t *rgb888_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
  fmt2rgb888(fb->buf, fb->len, fb->format, rgb888_matrix->item);

  // --- Resize the RGB888 frame to 96x96 in this example ---

  Serial.println("Resizing the frame buffer...");
  resized_matrix = dl_matrix3du_alloc(1, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, 3);
  image_resize_linear(resized_matrix->item, rgb888_matrix->item, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, 3, fb->width, fb->height);

  // --- Free memory ---

  dl_matrix3du_free(rgb888_matrix);
  esp_camera_fb_return(fb);

  classify();

  // --- Convert back the resized RGB888 frame to JPG to send it back to the web app ---

  Serial.println("Converting resized RGB888 frame to JPG...");
  uint8_t * _jpg_buf = NULL;
  fmt2jpg(resized_matrix->item, out_len, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, PIXFORMAT_RGB888, 80, &_jpg_buf, &out_len);

  // --- Prepare the HTML response ---

  String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <title>ESP32-CAM Object Detection - COP 5611</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background-color: #f0f0f0;
            margin: 0;
            padding: 20px;
        }
        h1 {
            color: #333;
            text-align: center;
        }
        h2 {
            color: #666;
        }
        pre {
            background-color: #fff;
            padding: 10px;
            border-radius: 5px;
            overflow-x: auto;
        }
        img {
            display: block;
            margin: 20px auto;
            max-width: 100%;
            height: auto;
            border: 2px solid #333;
            border-radius: 5px;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            background-color: #fff;
            padding: 20px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0, 0, 0, 0.1);
        }
        .class-info {
            text-align: center;
            margin-top: 20px;
            font-size: 18px;
            color: #666;
        }
    </style>
    </head>
    <body>
        <div class="container">
            <h1>ESP32-CAM Object Detection</h1>
            <h2>Prediction Output:</h2>
            <pre>)" + globalPredictionOutput + R"(</pre>
            <h2>Captured Image:</h2>
            <img src="data:image/jpeg;base64,)" + base64::encode(_jpg_buf, out_len) + R"(" alt="Captured Image">
            <div class="class-info">
                <p>COP 5611 - Operating Systems</p>
            </div>
        </div>
    </body>
    </html>
  )";

  // --- Send response ---

  Serial.println("Sending back HTTP response...");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "utf-8");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  res = httpd_resp_send(req, html.c_str(), html.length());

  // --- Free memory ---

  dl_matrix3du_free(resized_matrix);
  free(_jpg_buf);
  _jpg_buf = NULL;

  return res;
}

static esp_err_t page_handler(httpd_req_t *req) {

  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  // httpd_resp_send(req, page, sizeof(page));
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t capture_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };

  Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  }
}
void setupWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);

    // Wait for connection
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        retries++;
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFailed to connect to WiFi. Switching to AP mode");
        WiFi.softAP("ESP32-Cam AP", "12345678"); // Change SSID and password as needed
        Serial.println("IP address: ");
        Serial.println(WiFi.softAPIP());
    }
}

void loop() {
  ArduinoOTA.handle(); // Handle OTA updates
  delay(10);
}
