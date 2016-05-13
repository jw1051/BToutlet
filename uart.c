/* 
 * File:         uart.c
 * Author:       tbriggs
 * Revisions by: Knoll, Walthers, White
 *
 * Created on December 11, 2015
 */

#include <stdint.h>
#include <plib.h>
#include "uart.h"
#include "circularbuff.h"

/*
 * Buffers to hold TX & RX data...
 */
circbuff_t txbuff, rxbuff;
int send_xoff = 0;
int send_xon = 0;
int rcvd_xoff = 0;
int echo = 0;
int eolfix = 1;
int buffering = 0;

/*
 * Interrupt Service Routine for UART - handles rx and tx interrupts.
 */
void __ISR(_UART1_VECTOR, ipl5auto) isr_uart_handler(void) {

    char ch;

    // check of rx data -- drop character if buffer is full
    if (INTGetFlag(INT_SOURCE_UART_RX(UART1))) {
        // there was a received character, get the character and
        // add it to the buffer
        ch = UARTGetDataByte(UART1);

#ifdef USEXONXOFF
        if (ch == 0x13) {
            rcvd_xoff = 1;
        } else if (ch == 0x11) {
            rcvd_xoff = 0;
        } else
#endif
        {
            if (!circbuff_isfull(&rxbuff)) {
                circbuff_addch(&rxbuff, ch);
            }

            if (circbuff_almostfull(&rxbuff)) {
                send_xoff = 1;
            }
        }
        // clear the interrupt flag
        INTClearFlag(INT_SOURCE_UART_RX(UART1));
    }


    // tx has signalled, start up the next character
    if (INTGetFlag(INT_SOURCE_UART_TX(UART1))) {
        // clear the interrupt
        INTClearFlag(INT_SOURCE_UART_TX(UART1));
        
        if (circbuff_isempty(&txbuff)) {
            INTEnable(INT_SOURCE_UART_TX(UART1), INT_DISABLED);
        } else if (UARTTransmitterIsReady(UART1)) {

            // software flow-control
#ifdef USEXONXOFF
            if (send_xoff == 1) {
                UARTSendDataByte(UART1, 0x13);
                send_xoff = 0;
            } else if (send_xon == 1) {
                UARTSendDataByte(UART1, 0x11);
                send_xon = 0;
            } else if (rcvd_xoff == 1) {
                INTEnable(INT_SOURCE_UART_TX(UART1), INT_DISABLED);
            } else if (rcvd_xoff == 0) {
                ch = circbuff_getch(&txbuff);
                UARTSendDataByte(UART1, ch);
            }
#else
            ch = circbuff_getch(&txbuff);
            UARTSendDataByte(UART1, ch);
#endif
        }

        
    }

}

void uart_init (void)
{
    
    UARTConfigure(UART1, UART_ENABLE_PINS_TX_RX_ONLY);
    U1MODEbits.SIDL = 0;  // Ensures operation continues in idle mode
    UARTSetFifoMode(UART1, UART_INTERRUPT_ON_TX_BUFFER_EMPTY | UART_INTERRUPT_ON_RX_NOT_EMPTY);
    UARTSetLineControl(UART1, UART_DATA_SIZE_8_BITS | UART_PARITY_NONE | UART_STOP_BITS_1);
    UARTSetDataRate(UART1, PBCLK, BAUDRATE);
    UARTEnable(UART1, UART_ENABLE_FLAGS(UART_PERIPHERAL | UART_RX | UART_TX));
    
    INTClearFlag(INT_SOURCE_UART_RX(UART1));
    INTClearFlag(INT_SOURCE_UART_TX(UART1));
    
    INTSetVectorPriority(INT_VECTOR_UART(1), INT_PRIORITY_LEVEL_5);
    INTSetVectorSubPriority(INT_VECTOR_UART(1), INT_SUB_PRIORITY_LEVEL_0);
    
    INTEnable(INT_SOURCE_UART_TX(1), INT_ENABLED);
    INTEnable(INT_SOURCE_UART_RX(1), INT_ENABLED);
}

void uart_putc (char c)
{
    while (U1STAbits.UTXBF);  // wait until transmit buffer empty
    U1TXREG = c;              // transmit character over UART
}

int uart_getc(void)
{
    return U1RXREG;
}

void uart_puts (char *s)
{
    while (*s != '\0')        //while string still not empty
        uart_putc (*s++);     //put next char

}

int uart_haschar( ) {
    return !circbuff_isempty(&rxbuff);
}

void uart_echo(int enable) {
    echo = enable;
}

// standard I/O functions

void _mon_putc(char ch) {
    int wait_xon = 0;

    while (circbuff_isfull(&txbuff)) {
        if (rcvd_xoff == 1) wait_xon = 1;
        else if (wait_xon == 0) {
            INTEnable(INT_SOURCE_UART_TX(UART1), INT_ENABLED);
            INTSetFlag(INT_SOURCE_UART_TX(UART1));
        }
    };

    circbuff_addch(&txbuff, ch);
    INTEnable(INT_SOURCE_UART_TX(UART1), INT_ENABLED);
    INTSetFlag(INT_SOURCE_UART_TX(UART1));

}

//#define FAKE_GETC 1

int _mon_getc(int canblock) {

#ifdef FAKE_GETC
    static int fake_count = 0;

    int ret = -1;
    switch (fake_count) {
        case 0: ret = 'A';
            break;
        case 1: ret = 'B';
            break;
        case 2: ret = 'C';
            break;
        case 3: ret = 10;
            break;
        default: ret = -1;
    }
    fake_count = (fake_count + 1) % 4;
    return ret;
#else
    static char last_char = 0;

    if ((buffering == 1) || (canblock == 0)) {
        if (circbuff_isempty(&rxbuff)) return -1;
        char ch = circbuff_getch(&rxbuff);
        return ch;
    }

_mon_getc_L1:
    while (circbuff_isempty(&rxbuff));
    char ch = circbuff_getch(&rxbuff);

    if (eolfix == 1) {

        if ((ch == '\n') && (last_char == '\r'))
            goto _mon_getc_L1;

        last_char = ch;

        if ( ch == '\r')
            ch = '\n';
    }

    if (echo == 1)
        _mon_putc(ch);

    return ch;

#endif
}

void _mon_puts(const char *s) {
    while (*s != 0x00) {
        _mon_putc(*s++);
    }
}

void _mon_write(const char *s, unsigned int count) {
    int i;
    for (i = 0; i < count; i++) {
        _mon_putc(s[i]);
    }
}

//#define CUSTOM_FGETS

#ifdef CUSTOM_FGETS
char *	fgets(char *ptr, int num, FILE *stream)
#else
char *	myfgets(char *ptr, int num, FILE *stream)
#endif

{
    
    int i = 0;
    while (i < (num-1))
    {
        int ch = (stream == stdin) ? _mon_getc(1) : fgetc(stream);
        
        // handle backspace
        if ((ch == 0x7f) || (ch == 0x08)) {
            ptr[i] = 0x00;
            i = i - 1;
            if (i < 0) i = 0;
            continue;
        }

        if (ch <= 0xff) {
            ptr[i] = ch;
            ptr[i+1] = 0x00;
            i++;
        }
        
        if (ch == 0x03) {
            if (stream == stdin) {
                _mon_puts("^C");
            }
            return NULL;
        }
        if ((i == 0) && (ch < 0)) return NULL;

        if ((i == 0) && (ch == 0x04)) {
            if (stream == stdin) {
                _mon_puts("^D");
                return NULL;
            }

        }

        if ((ch == 0x0d) || (ch == 0x0a) || (ch == 0x04) || (ch < 0))
            break;
    }

    ptr[i] = 0x00;
    return ptr;
}



// dump string to the output buffer - drop chars if the buffer is full
// and don't generate interrupts
void uart_fast_puts(const char *str)
{
    if (circbuff_isfull(&txbuff)) return;

    while ((*str != 0) && (!circbuff_isfull(&txbuff)))
    {
        circbuff_addch(&txbuff, *str++);
        
    }

    // generate an interrupt to flush the buffer
    INTEnable(INT_SOURCE_UART_TX(UART1), INT_ENABLED);
    INTSetFlag(INT_SOURCE_UART_TX(UART1));

}
