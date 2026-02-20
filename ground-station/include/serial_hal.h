/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      serial_hal.h
 * Desc:      Cross-platform Hardware Abstraction Layer for USB/Serial.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef SERIAL_HAL_H
#define SERIAL_HAL_H

#include <cstdint>
#include <cstddef>

// Initialize the serial port (e.g., "COM3" or "/dev/tty.usbmodem14101")
bool serial_open(const char* port_name, uint32_t baud_rate);

// Read bytes directly into a pre-allocated static buffer (No Heap)
// Returns the number of bytes read, or -1 on error.
int serial_read_bytes(uint8_t* buffer, size_t max_bytes);

// Safely release the hardware lock
void serial_close();

// Write bytes out to the hardware
int serial_write_bytes(const uint8_t* buffer, size_t len);

#endif