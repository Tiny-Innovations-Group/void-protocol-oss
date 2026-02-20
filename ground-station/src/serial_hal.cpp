/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      serial_hal.cpp
 * Desc:      OS-specific implementation of the Serial HAL.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "serial_hal.h"

// =================================================================-------
// WINDOWS IMPLEMENTATION
// =================================================================-------
#ifdef _WIN32
#include <windows.h>

static HANDLE hSerial = INVALID_HANDLE_VALUE;

bool serial_open(const char* port_name, uint32_t baud_rate) {
    hSerial = CreateFileA(port_name, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hSerial == INVALID_HANDLE_VALUE) return false;

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) return false;

    dcbSerialParams.BaudRate = baud_rate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity   = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) return false;

    // Non-blocking reads
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    SetCommTimeouts(hSerial, &timeouts);

    return true;
}

int serial_read_bytes(uint8_t* buffer, size_t max_bytes) {
    if (hSerial == INVALID_HANDLE_VALUE) return -1;
    DWORD bytes_read;
    if (ReadFile(hSerial, buffer, static_cast<DWORD>(max_bytes), &bytes_read, NULL)) {
        return static_cast<int>(bytes_read);
    }
    return -1;
}

void serial_close() {
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
}

int serial_write_bytes(const uint8_t* buffer, size_t len) {
    if (hSerial == INVALID_HANDLE_VALUE) return -1;
    DWORD bytes_written;
    if (WriteFile(hSerial, buffer, static_cast<DWORD>(len), &bytes_written, NULL)) {
        return static_cast<int>(bytes_written);
    }
    return -1;
}

// =================================================================-------
// MAC / LINUX (POSIX) IMPLEMENTATION
// =================================================================-------
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

static int serial_fd = -1;

bool serial_open(const char* port_name, uint32_t baud_rate) {
    (void)baud_rate; // Suppress unused warning (assuming 115200 for demo)
    
    // Open in non-blocking read/write mode
    serial_fd = open(port_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (serial_fd == -1) return false;

    struct termios options;
    tcgetattr(serial_fd, &options);

    // Set Baud Rate to 115200
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    // 8N1 standard configuration
    options.c_cflag |= (CLOCAL | CREAD);
    // options.c_cflag &= ~PARENB;
    // options.c_cflag &= ~CSTOPB;
    // options.c_cflag &= ~CSIZE;
    // options.c_cflag |= CS8;

    // 1. Fix C_CFLAG (Control Modes)
    options.c_cflag &= ~static_cast<tcflag_t>(PARENB); // Clear parity bit
    options.c_cflag &= ~static_cast<tcflag_t>(CSTOPB); // Clear stop field
    options.c_cflag &= ~static_cast<tcflag_t>(CSIZE);  // Clear size bits
    options.c_cflag |= static_cast<tcflag_t>(CS8);    // 8-bit characters

    tcflag_t local_mask = static_cast<tcflag_t>(ICANON | ECHO | ECHOE | ISIG);
    options.c_lflag &= ~local_mask;

    options.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
    
    // // Raw input (no terminal echo or line buffering)
    // options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    tcsetattr(serial_fd, TCSANOW, &options);
    fcntl(serial_fd, F_SETFL, FNDELAY);

    return true;
}

int serial_read_bytes(uint8_t* buffer, size_t max_bytes) {
    if (serial_fd == -1) return -1;
    int bytes_read = static_cast<int>(read(serial_fd, buffer, max_bytes));
    return (bytes_read > 0) ? bytes_read : 0;
}

void serial_close() {
    if (serial_fd != -1) {
        close(serial_fd);
        serial_fd = -1;
    }
}


int serial_write_bytes(const uint8_t* buffer, size_t len) {
    if (serial_fd == -1) return -1;
    int bytes_written = static_cast<int>(write(serial_fd, buffer, len));
    return (bytes_written > 0) ? bytes_written : 0;
}
#endif