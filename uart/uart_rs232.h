#ifndef UART_RS232_H
#define UART_RS232_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// RS232 포트별 고정 핀 번호 (예시)
#define UART_RS232_1_BAUD 9600
#define RS232_1_TX_PIN 0
#define RS232_1_RX_PIN 1


#ifdef __cplusplus
extern "C"
{
#endif

  typedef enum
  {
    RS232_PORT_1 = 0
  } rs232_port_t;

  typedef struct
  {
    rs232_port_t port;
    uint8_t tx_pin;
    uint8_t rx_pin;
    uint32_t baudrate;
  } uart_rs232_config_t;

  // RS232 포트 기본 baudrate (전역 변수)
  extern uint32_t uart_rs232_1_baud;

  void save_uart_rs232_baud_to_flash(void);
  bool uart_rs232_init(rs232_port_t port, uint32_t baudrate);
  bool uart_rs232_write(rs232_port_t port, const uint8_t *data, uint32_t len);
  void uart_rs232_process(void);

#ifdef __cplusplus
}
#endif

#endif // UART_RS232_H
