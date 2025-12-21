// LED 테스트 프로그램
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("LED Test Program\n");
    printf("Testing GPIO 24, 25\n");
    
    // GPIO 24 테스트
    gpio_init(24);
    gpio_set_dir(24, GPIO_OUT);
    
    // GPIO 25 테스트
    gpio_init(25);
    gpio_set_dir(25, GPIO_OUT);
    
    while (true) {
        printf("GPIO 24 ON, GPIO 25 OFF\n");
        gpio_put(24, 1);
        gpio_put(25, 0);
        sleep_ms(1000);
        
        printf("GPIO 24 OFF, GPIO 25 ON\n");
        gpio_put(24, 0);
        gpio_put(25, 1);
        sleep_ms(1000);
        
        printf("Both ON\n");
        gpio_put(24, 1);
        gpio_put(25, 1);
        sleep_ms(1000);
        
        printf("Both OFF\n");
        gpio_put(24, 0);
        gpio_put(25, 0);
        sleep_ms(1000);
    }
}
