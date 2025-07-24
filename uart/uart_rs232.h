#ifndef UART_RS232_H
#define UART_RS232_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

// RS232 포트별 고정 핀 번호 (예시)
#define UART_RS232_1_BAUD 9600
#define UART_RS232_2_BAUD 9600
#define RS232_1_TX_PIN 4
#define RS232_1_RX_PIN 5
#define RS232_2_TX_PIN 8
#define RS232_2_RX_PIN 9

#define UART_RS232_BAUD_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 12288) // 마지막에서 세 번째 4KB

#ifdef __cplusplus
extern "C"
{
  #endif
  
  
  typedef enum {
    RS232_PORT_1 = 0,
    RS232_PORT_2 = 1
  } rs232_port_t;
  
  typedef struct {
    rs232_port_t port;
    uint8_t tx_pin;
    uint8_t rx_pin;
    uint32_t baudrate;
  } uart_rs232_config_t;
  
  
  // 각 포트별 기본 baudrate (전역 변수)
  extern uint32_t uart_rs232_1_baud;
  extern uint32_t uart_rs232_2_baud;
  
  void save_uart_rs232_baud_to_flash(void);
  void load_uart_rs232_baud_from_flash(void);
  void uart_rs232_init(rs232_port_t port, uint32_t baudrate);
  // void uart_rs232_init_ex(const uart_rs232_config_t* config);
  int uart_rs232_write(rs232_port_t port, const uint8_t *data, uint32_t len);
  int uart_rs232_read(rs232_port_t port, uint8_t *buf, uint32_t maxlen);
  bool uart_rs232_available(rs232_port_t port);

#ifdef __cplusplus
}
#endif

#endif // UART_RS232_H
