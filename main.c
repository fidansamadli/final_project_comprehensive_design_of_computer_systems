/*
 * ============================================================================
 * Final project - LED Morse Code Messenger
 *              基于龙芯 1B 开发板的 LED 摩尔斯电码通信系统
 * ----------------------------------------------------------------------------
 * Platform : Loongson 1B (LS1B200, LS232, MIPS32 little-endian)
 *            RT-Thread 4.0.2 board support package
 * Console  : UART5 (board console), 115200 baud, 8 data bits, no parity, 1 stop bit
 * Output   : the eight active-low LEDs of the course LED module
 *
 * The application receives a short text message on the serial port, encodes it
 * into International Morse Code and transmits it on the LED module.  A dot is
 * 1 time unit of light, a dash is 3 units, the gap between the elements of one
 * character is 1 unit, the gap between characters is 3 units and the gap
 * between words is 7 units.  One unit lasts 1200 / WPM milliseconds.
 *
 * The transmission is a non-blocking state machine.  The main loop polls the
 * UART with a bounded budget and advances the Morse timeline from the RT-Thread
 * tick, so a command such as STOP is accepted while the LEDs are flashing.
 *
 * Commands (terminated by CR, LF or CRLF):
 *   MSG <text>   encode and transmit <text> (one pending message is queued)
 *   REPEAT       transmit the last message again
 *   SPEED <wpm>  set the speed, 5..30 words per minute
 *   STOP         abort the current transmission and switch the LEDs off
 *   RESET        stop, forget the message and restore the default speed
 *   STATUS       report state, speed and progress
 *   HELP         print the command list
 *
 * ============================================================================
 */

#include <stddef.h>

#ifdef MORSE_HOST_TEST
#include "tests/host_platform.h"
#else
#include "ls1b.h"
#include "mips.h"
#include "ls1b_gpio.h"
#include "ns16550.h"
#include "rtthread.h"
#endif

/* ---------------------------------------------------------------- hardware */
#define LED_1 9
#define LED_2 11
#define LED_3 15
#define LED_4 17
#define LED_5 20
#define LED_6 22
#define LED_7 6
#define LED_8 7

/* ---------------------------------------------------------------- protocol */
#define MORSE_MAX_MSG      40
#define MORSE_MAX_EVENTS   360
#define WPM_MIN            5
#define WPM_MAX            30
#define WPM_DEFAULT        12
#define BUFFER_SIZE        48
#define UART_POLL_BUDGET   64

#define STATE_IDLE         0
#define STATE_SENDING      1

typedef struct
{
    unsigned char  on;          /* 1 = lamp on (mark), 0 = gap          */
    unsigned short units;       /* duration in Morse time units          */
} morse_event;

typedef struct
{
    char        symbol;
    const char *code;
} morse_entry;

/* ITU-R M.1677-1 letters, digits and the punctuation used by the course. */
static const morse_entry morse_table[] =
{
    { 'A', ".-"    }, { 'B', "-..."  }, { 'C', "-.-."  }, { 'D', "-.."   },
    { 'E', "."     }, { 'F', "..-."  }, { 'G', "--."   }, { 'H', "...."  },
    { 'I', ".."    }, { 'J', ".---"  }, { 'K', "-.-"   }, { 'L', ".-.."  },
    { 'M', "--"    }, { 'N', "-."    }, { 'O', "---"   }, { 'P', ".--."  },
    { 'Q', "--.-"  }, { 'R', ".-."   }, { 'S', "..."   }, { 'T', "-"     },
    { 'U', "..-"   }, { 'V', "...-"  }, { 'W', ".--"   }, { 'X', "-..-"  },
    { 'Y', "-.--"  }, { 'Z', "--.."  },
    { '0', "-----" }, { '1', ".----" }, { '2', "..---" }, { '3', "...--" },
    { '4', "....-" }, { '5', "....." }, { '6', "-...." }, { '7', "--..." },
    { '8', "---.." }, { '9', "----." },
    { '.', ".-.-.-"}, { ',', "--..--"}, { '?', "..--.."}, { '/', "-..-." },
    { '-', "-....-"}, { ':', "---..."}
};

#define MORSE_TABLE_SIZE ((int)(sizeof(morse_table) / sizeof(morse_table[0])))

/* ------------------------------------------------------------------ state */
static const int led_pins[8] =
{
    LED_1, LED_2, LED_3, LED_4, LED_5, LED_6, LED_7, LED_8
};

static morse_event events[MORSE_MAX_EVENTS];
static int event_count;
static int event_index;
static unsigned int event_remaining_ms;
static unsigned int service_tick;

static int state = STATE_IDLE;
static int wpm = WPM_DEFAULT;

static char message[MORSE_MAX_MSG + 1];
static char pending[MORSE_MAX_MSG + 1];
static int message_chars;
static int message_skipped;
static int message_truncated;

static unsigned int start_tick;
static unsigned int sent_chars;
static unsigned int sent_ms;

static char buffer_uart[BUFFER_SIZE];
static int total_read;
static int overflow;

/* --------------------------------------------------------------- platform */
#ifdef MORSE_HOST_TEST

static void out_text(const char *text)
{
    host_uart_send(text);
}

static unsigned int now_ms(void)
{
    return host_now();
}

static int uart_read_byte(char *byte)
{
    return host_uart_read(byte);
}

#else

static void uart_send(const char *text)
{
    char ch;
    while ((ch = *text++) != '\0')
        ls1x_uart_write(devUART5, &ch, 1, NULL);
}

static void out_text(const char *text)
{
    uart_send(text);
}

static unsigned int now_ms(void)
{
    return (unsigned int)rt_tick_get();
}

static int uart_read_byte(char *byte)
{
    return ls1x_uart_read(devUART5, byte, 1, NULL);
}

static int uart_init(void)
{
    if (ls1x_uart_init(devUART5, NULL) != 0)
        return -1;
    return ls1x_uart_open(devUART5, NULL);
}

static void lamp_init(void)
{
    int i;
    /* Preload the inactive level before enabling the output drivers. */
    for (i = 0; i < 8; ++i)
        gpio_write(led_pins[i], 1);
    for (i = 0; i < 8; ++i)
        gpio_enable(led_pins[i], DIR_OUT);
}

#endif

/* All eight LEDs form one visible lamp.  Active low: 0 = light. */
static void lamp(int on)
{
    int i;
    for (i = 0; i < 8; ++i)
        gpio_write(led_pins[i], on ? 0 : 1);
}

/* ------------------------------------------------------------ text output */
static void out_uint(unsigned int value)
{
    char digits[12];
    int n = 0;
    if (value == 0)
    {
        out_text("0");
        return;
    }
    while (value > 0 && n < (int)sizeof(digits) - 1)
    {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n > 0)
    {
        char one[2];
        one[0] = digits[--n];
        one[1] = '\0';
        out_text(one);
    }
}

/* ------------------------------------------------------------- Morse data */
static const char *morse_code(char c)
{
    int i;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (i = 0; i < MORSE_TABLE_SIZE; ++i)
    {
        if (morse_table[i].symbol == c)
            return morse_table[i].code;
    }
    return NULL;
}

static unsigned int unit_ms(void)
{
    return 1200u / (unsigned int)wpm;
}

static int add_event(int on, unsigned int units)
{
    if (event_count >= MORSE_MAX_EVENTS)
        return 0;
    events[event_count].on = (unsigned char)(on ? 1 : 0);
    events[event_count].units = (unsigned short)units;
    event_count++;
    return 1;
}

/*
 * Encode a NUL-terminated text into the event array.  Returns the number of
 * characters that were encoded; *skipped receives the number of characters
 * that have no Morse representation.  A word space widens the preceding gap
 * to seven units instead of adding a second gap.
 */
static int morse_encode(const char *text, int *skipped, int *truncated)
{
    int chars = 0;
    int last_was_space = 1;

    event_count = 0;
    *skipped = 0;
    *truncated = 0;

    while (*text != '\0')
    {
        char c = *text++;
        const char *code;
        int i, len;

        if (c == ' ' || c == '\t')
        {
            if (!last_was_space && event_count > 0)
            {
                /* The character gap (3) becomes a word gap (7). */
                events[event_count - 1].units = 7;
            }
            last_was_space = 1;
            continue;
        }

        code = morse_code(c);
        if (code == NULL)
        {
            (*skipped)++;
            continue;
        }

        len = 0;
        while (code[len] != '\0')
            len++;

        for (i = 0; i < len; ++i)
        {
            if (!add_event(1, code[i] == '-' ? 3u : 1u))
            {
                *truncated = 1;
                goto finish;
            }
            if (i + 1 < len)
            {
                if (!add_event(0, 1))
                {
                    *truncated = 1;
                    goto finish;
                }
            }
        }
        if (!add_event(0, 3))
        {
            *truncated = 1;
            goto finish;
        }
        chars++;
        last_was_space = 0;
    }

finish:
    /* A transmission ends when the light ends: drop the trailing gap. */
    if (event_count > 0 && events[event_count - 1].on == 0)
        event_count--;
    return chars;
}

/* ------------------------------------------------------------ transmitter */
static void start_event(int index)
{
    unsigned int duration;

    event_index = index;
    duration = (unsigned int)events[index].units * unit_ms();
    event_remaining_ms = duration;
    lamp(events[index].on);
}

static void begin_transmission(const char *text)
{
    int skipped = 0, truncated = 0;
    int chars = morse_encode(text, &skipped, &truncated);

    message_chars = chars;
    message_skipped = skipped;
    message_truncated = truncated;

    if (chars == 0 || event_count == 0)
    {
        state = STATE_IDLE;
        lamp(0);
        out_text("ERR NO ENCODABLE CHARACTERS\r\n");
        return;
    }

    state = STATE_SENDING;
    sent_chars = 0;
    sent_ms = 0;
    start_tick = now_ms();
    service_tick = start_tick;
    start_event(0);

    out_text("SEND ");
    out_uint((unsigned int)chars);
    out_text(" CHARS AT ");
    out_uint((unsigned int)wpm);
    out_text(" WPM");
    if (skipped > 0)
    {
        out_text(", SKIP ");
        out_uint((unsigned int)skipped);
    }
    if (truncated)
        out_text(", TRUNCATED");
    out_text("\r\n");
}

static void finish_transmission(unsigned int now)
{
    sent_chars = (unsigned int)message_chars;
    sent_ms = now - start_tick;
    state = STATE_IDLE;
    lamp(0);

    out_text("DONE ");
    out_uint((unsigned int)message_chars);
    out_text(" CHARS IN ");
    out_uint(sent_ms);
    out_text(" MS\r\n");

    if (pending[0] != '\0')
    {
        int i = 0;
        while (pending[i] != '\0' && i < MORSE_MAX_MSG)
        {
            message[i] = pending[i];
            i++;
        }
        message[i] = '\0';
        pending[0] = '\0';
        out_text("NEXT\r\n");
        begin_transmission(message);
    }
}

static void stop_transmission(const char *reason)
{
    state = STATE_IDLE;
    pending[0] = '\0';
    lamp(0);
    out_text(reason);
    out_text("\r\n");
}

/*
 * Advance the Morse timeline.  Called from the main loop with the current
 * millisecond tick; the elapsed time is consumed by whole events so a delayed
 * call cannot stretch or drop an element.
 */
void morse_service(unsigned int now)
{
    unsigned int elapsed;

    if (state != STATE_SENDING)
        return;

    elapsed = now - service_tick;
    service_tick = now;

    while (state == STATE_SENDING && elapsed >= event_remaining_ms)
    {
        elapsed -= event_remaining_ms;
        if (event_index + 1 >= event_count)
        {
            finish_transmission(now);
            return;
        }
        start_event(event_index + 1);
    }

    if (state == STATE_SENDING)
        event_remaining_ms -= elapsed;
}

/* --------------------------------------------------------------- commands */
static void reply_ok(void)
{
    out_text("OK\r\n");
}

static void cmd_help(void)
{
    out_text("COMMANDS: MSG <text> | REPEAT | SPEED <wpm> | STOP | RESET | STATUS | HELP\r\n");
    out_text("EXAMPLE: MSG SOS\r\n");
}

static void cmd_msg(const char *text)
{
    int i = 0;
    char copy[MORSE_MAX_MSG + 1];

    while (*text == ' ' || *text == '\t')
        text++;
    while (text[i] != '\0' && i < MORSE_MAX_MSG)
    {
        copy[i] = text[i];
        i++;
    }
    copy[i] = '\0';

    if (i == 0)
    {
        out_text("ERR EMPTY MESSAGE\r\n");
        return;
    }

    if (state == STATE_SENDING)
    {
        int j = 0;
        while (copy[j] != '\0')
        {
            pending[j] = copy[j];
            j++;
        }
        pending[j] = '\0';
        out_text("QUEUED\r\n");
        return;
    }

    {
        int j = 0;
        while (copy[j] != '\0')
        {
            message[j] = copy[j];
            j++;
        }
        message[j] = '\0';
    }
    pending[0] = '\0';
    begin_transmission(message);
}

static void cmd_repeat(void)
{
    if (state == STATE_SENDING)
    {
        out_text("ERR BUSY\r\n");
        return;
    }
    if (message[0] == '\0')
    {
        out_text("ERR NO MESSAGE\r\n");
        return;
    }
    begin_transmission(message);
}

static void cmd_speed(const char *text)
{
    int value = 0;
    int digits = 0;

    while (*text == ' ' || *text == '\t')
        text++;
    while (*text >= '0' && *text <= '9')
    {
        value = value * 10 + (*text - '0');
        if (value > 1000)
            value = 1000;
        text++;
        digits++;
    }
    while (*text == ' ' || *text == '\t')
        text++;
    if (digits == 0 || *text != '\0' || value < WPM_MIN || value > WPM_MAX)
    {
        out_text("ERR SPEED 5..30\r\n");
        return;
    }
    wpm = value;
    out_text("OK ");
    out_uint((unsigned int)wpm);
    out_text(" WPM ");
    out_uint(unit_ms());
    out_text(" MS\r\n");
}

static void cmd_stop(void)
{
    if (state != STATE_SENDING)
    {
        lamp(0);
        pending[0] = '\0';
        out_text("IDLE\r\n");
        return;
    }
    stop_transmission("STOPPED");
}

static void cmd_reset(void)
{
    state = STATE_IDLE;
    lamp(0);
    message[0] = '\0';
    pending[0] = '\0';
    message_chars = 0;
    wpm = WPM_DEFAULT;
    out_text("RESET OK\r\n");
    out_text("MORSE MESSENGER READY\r\n");
}

static void cmd_status(void)
{
    out_text("STATUS ");
    out_text(state == STATE_SENDING ? "SENDING" : "IDLE");
    out_text(" WPM=");
    out_uint((unsigned int)wpm);
    out_text(" UNIT=");
    out_uint(unit_ms());
    out_text("MS");
    if (state == STATE_SENDING)
    {
        out_text(" EVENT=");
        out_uint((unsigned int)(event_index + 1));
        out_text("/");
        out_uint((unsigned int)event_count);
    }
    out_text(" MSG=");
    out_text(message[0] != '\0' ? message : "-");
    if (pending[0] != '\0')
        out_text(" PENDING=YES");
    out_text("\r\n");
}

static int equals(const char *text, const char *word)
{
    while (*word != '\0')
    {
        if (*text++ != *word++)
            return 0;
    }
    return *text == '\0';
}

static int starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0')
    {
        if (*text++ != *prefix++)
            return 0;
    }
    return 1;
}

static int is_blank(char c)
{
    return c == ' ' || c == '\t';
}

static void parse_command(const char *command)
{
    char line[BUFFER_SIZE];
    int len = 0;

    while (is_blank(*command))
        command++;
    while (*command != '\0' && len < BUFFER_SIZE - 1)
    {
        char c = *command++;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        line[len++] = c;
    }
    if (*command != '\0')
    {
        out_text("ERR\r\n");
        return;
    }
    while (len > 0 && is_blank(line[len - 1]))
        len--;
    line[len] = '\0';
    if (len == 0)
        return;

    if (starts_with(line, "MSG") && is_blank(line[3]))
        cmd_msg(line + 3);
    else if (equals(line, "MSG"))
        out_text("ERR EMPTY MESSAGE\r\n");
    else if (equals(line, "REPEAT"))
        cmd_repeat();
    else if (starts_with(line, "SPEED") && is_blank(line[5]))
        cmd_speed(line + 5);
    else if (equals(line, "STOP"))
    {
        cmd_stop();
        reply_ok();
    }
    else if (equals(line, "RESET"))
    {
        cmd_reset();
        reply_ok();
    }
    else if (equals(line, "STATUS"))
        cmd_status();
    else if (equals(line, "HELP"))
        cmd_help();
    else
        out_text("ERR\r\n");
}

/* ------------------------------------------------------------ UART input */
void poll_uart(void)
{
    char byte;
    int budget = UART_POLL_BUDGET;

    /* Bounded work: a continuous stream must not starve the Morse timing. */
    while (budget-- > 0 && uart_read_byte(&byte) > 0)
    {
        unsigned char c = (unsigned char)byte;
        if (c == '\r' || c == '\n')
        {
            if (overflow)
                out_text("ERR LINE TOO LONG\r\n");
            else if (total_read > 0)
            {
                buffer_uart[total_read] = '\0';
                parse_command(buffer_uart);
            }
            total_read = 0;
            overflow = 0;
        }
        else if (overflow)
        {
            continue;               /* discard the damaged line */
        }
        else if (c == '\b' || c == 0x7f)
        {
            if (total_read > 0)
                total_read--;
        }
        else if (c == '\t' || (c >= 0x20 && c <= 0x7e))
        {
            if (total_read < BUFFER_SIZE - 1)
                buffer_uart[total_read++] = (char)c;
            else
                overflow = 1;
        }
        else
        {
            overflow = 1;           /* never accept a line with garbage */
        }
    }
}

#ifndef MORSE_HOST_TEST
int main(void)
{
    lamp_init();
    ls1x_drv_init();
    install_3th_libraries();
    if (uart_init() != 0)
    {
        rt_kprintf("UART5 initialization failed\n");
        return -1;
    }
    service_tick = now_ms();
    out_text("MORSE MESSENGER READY\r\n");
    out_text("TYPE HELP FOR COMMANDS\r\n");

    while (1)
    {
        poll_uart();
        morse_service(now_ms());
        /* One scheduler tick keeps the console responsive between elements. */
        rt_thread_delay(1);
    }
}
#endif

/*
 * @@ End
 */
