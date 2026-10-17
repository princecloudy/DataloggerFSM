## 💡 Overview
<p align="justify">
Designed and developed an ESP32-based temperature and humidity data logger using a Finite State Machine (FSM), featuring configurable sampling intervals, MicroSD data logging, an OLED user interface, and a safe remove MicroSD.
</p>

## ✨ Features

- **🌡️ Real-Time Temperature and Humidity Monitoring:** Measure ambient temperature and humidity using a DHT11 sensor connected to an ESP32.
- **⏱️ Configurable Sampling Intervals:** Adjust data sampling intervals from 1 to 3600 seconds to suit different data logging requirements.
- **💾 Automated Data Logging:** Record sensor readings and timestamps to a MicroSD card in CSV format for easy data analysis and processing.
- **🕒 Real-Time Clock Integration:** Use a DS3231 RTC module to provide date and time information for recorded measurements.
- **🖥️ OLED User Interface:** Display sensor readings, system states, and configuration menus on a 0.96-inch SSD1306 OLED screen.
- **🔄 Finite State Machine (FSM):** Organize system operations into defined states to manage sensor readings, sampling intervals, menu navigation, and data logging.
- **🛡️ Safe Remove MicroSD:** Provide a dedicated safe removal function to help prevent data corruption when removing the MicroSD card.
- **🔘 Button-Based Controls:** Navigate menus, adjust sampling intervals, and save removing MicroSD through physical push buttons.
