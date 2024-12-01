# Microcontroller Object Detection (ESP32 Cam)

This project uses the **ESP32 Cam** to do something pretty unique: detect objects and share the results with a web browser—all from a tiny, inexpensive microcontroller! It’s a mix of embedded programming, object detection, and problem-solving on limited hardware.

---

## What’s the Goal?

The idea was to make a fully self-contained object detection system. No powerful computers, no fancy cloud services—just the ESP32 Cam working on its own. It captures images, detects objects, and sends everything to your browser for you to see.

### What It Does:
- Captures images using the ESP32 Cam’s built-in camera.
- Runs custom object detection on those images (trained with Edge Impulse).
- Sends the results, including bounding boxes and confidence scores, to a web browser via an IP address.

---
![Example](https://github.com/user-attachments/assets/d74e6724-79b7-4e76-b167-12245aa513c8)


## How It Works

The ESP32 Cam has some pretty modest specs, which made this a challenge. Here’s how it all comes together:
- **Object Detection**: Trained on 100 custom images using Edge Impulse with the FOMO model (MobileNetV2).
- **Web Interface**: Serves the detection results over Wi-Fi to any device on the same network.
- **Programming**: Used FreeRTOS for multitasking and libraries like `esp_camera.h` and `esp_http_server.h`.

Instead of live video, the system processes one image at a time when the browser refreshes (the hardware’s memory and processing power couldn’t handle both detection and video streaming together).

---
![ESP32 Wiring](https://github.com/user-attachments/assets/4eb36ed6-1203-41c3-b990-840181b46aec)
## What Makes It Cool?

- **Affordable Hardware**: The ESP32 Cam costs about $6, making it an awesome option for DIY projects.
- **Standalone System**: Everything happens on the microcontroller—no need for external processors.
- **Endless Possibilities**: This can pave the way for custom object detection in robotics, home security, or even door locks that recognize people!

---

## What’s Next?

While this project achieved the core functionality, there’s still more to explore:
- Combining live video streaming and object detection at decent frame rates.
- Expanding the model to detect multiple objects accurately.
- Building practical applications like smart cameras or real-time monitoring systems.

---

## Lessons Learned

This project wasn’t just about coding—it involved a lot of trial and error with hardware quirks. From inconsistent power issues to finicky camera connections, getting everything to work was half the battle. But in the end, it was all worth it for what the ESP32 Cam could do.

---
