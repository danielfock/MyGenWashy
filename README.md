# MyGenWashy
Generic washing Machine Controller

**Hardware** — Result of the TuttleButtle Hackathon 2025 (13.–15.6.2025, Vienna, Austria). Clemens, Thomas & Patrick / MayerMakes got a broken washing machine and set the task to create a generic open source washing machine controller. The original washing machine and its electronics and a chinese replacement board were partly reverse engineered to understand the functionality. The team tested their theories on how safety mechanisms and machine parts worked and had to be controlled. At the end of day two the machine was running a cold wash program from a dev kit and some homemade electronics controlled via ESPHome. On day three the learnings and circuits were converted into a KiCad design for further testing and software development.

**Software** — Developed as part of the #IMM2026 Twin City Future Tech Bootcamp by Daniel, Kevin & Max. The Arduino Nano base controller firmware (TRIAC motor control, I2C slave, safety watchdogs) and the ESP32 program controller firmware (wash state machine, Web UI, REST API, MQTT Auto-Discovery) were built from scratch during the bootcamp.

## Architecture

The controller uses a two-MCU design with separation of concerns for safety:

**Base Controller (Arduino Nano / ATmega328P)** — low-level hardware control: TRIAC phase-angle motor control via zero-cross detection, RPM ramp up/down, relay command execution, NTC temperature monitoring, hardware watchdog. Contains no wash program logic.

**Program Controller (ESP32)** — high-level logic: wash program state machine (fill → heat → wash → drain → rinse → spin → unlock), water level sensing, dark-theme Web UI with program management, REST API, MQTT Auto-Discovery for Home Assistant. Communicates with the base controller via I2C.

Communication: register-based I2C protocol (slave address `0x20`, 100 kHz). The ESP32 writes relay/motor requests and reads temperature, RPM, flags, and error state. A 500 ms heartbeat keeps the base controller's 2-second watchdog alive. See [I2C_PROTOKOLL.md](I2C_PROTOKOLL.md) for the full register map.

## Features

- 7 predefined wash programs (20/30/40/60/90°C, Quick, Spin-only) with realistic wash motion profiles (Normal, Gentle, Wool)
- Custom wash programs stored as JSON on ESP32 flash (LittleFS)
- Web UI for program selection, monitoring, and custom program creation
- MQTT Auto-Discovery for Home Assistant (sensors, buttons, select entity)
- 3 water level modes on ESP32: float switches (production), potentiometer (hardware test), timer simulation (software-only test)
- Poti test mode on Nano for development without real hardware (A1=temp, A2=RPM)
- Persistent integration settings (MQTT server, topics) stored on ESP32 flash via LittleFS

## Repository

- `3226TEST/` — ATTiny3226 low-level controller (original hardware)
- `NanoWashy/` — Arduino Nano low-level controller (recommended for development)
- `ESP32Washy/` — ESP32 high-level controller (Arduino framework, PlatformIO)
- `esphome/` — earlier ESPHome-based reference (legacy)
- `mr_washi/` — KiCad hardware design files

See [SETUP_ANLEITUNG.md](SETUP_ANLEITUNG.md) for build, flash, and wiring instructions.

the board is intended to be locally produceable, by hand soldering and easy to repair. We avoided "special" components for easy component substitution.
Unit cost is not a concerning factor, this is about the availability of a workable design that can be fitted with whatever components are available.

Thanks to Aisler for the Protoype PCBs and Farnell for supporting us with parts!

![mr_washi_angled](https://github.com/user-attachments/assets/1646637f-5b61-4bc1-bef8-ef897bd06827)

![mr_washi_back_angled](https://github.com/user-attachments/assets/607a8444-5803-4847-ab29-120f63d233b3)

Find the Presentation PDF here:

[Hackathon EN-finial3.pdf](https://github.com/user-attachments/files/20941595/Hackathon.EN-finial3.pdf)
