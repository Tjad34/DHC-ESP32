# DHC ESP32 Project

This project implements DHC (Digital Health Certificate) functionality on an ESP32 microcontroller.

## Project Structure

- `components/dhc/` - DHC implementation component
- `main/` - Main application code
- `data/` - Data files for the project

## Building the Project

This project uses ESP-IDF (Espressif IoT Development Framework). To build and flash:

1. Install ESP-IDF following the [official guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
2. Set up ESP-IDF environment variables
3. Build the project:
   ```bash
   idf.py build
   ```
4. Flash to your ESP32:
   ```bash
   idf.py -p (PORT) flash
   ```

## License

[Add your chosen license here] 