/*
 * Host tests for the LED Morse Code Messenger.
 *
 * The real application source (../main.c) is compiled into this test program
 * with MORSE_HOST_TEST defined, so the encoder, the timing state machine, the
 * command parser and the UART line assembler under test are exactly the code
 * that is cross-compiled for the Loongson 1B board.  Only the board services
 * (GPIO, UART, tick) are replaced.
 *
 * Build and run:
 *   gcc -std=c99 -Wall -Wextra -Werror -I.. -o test_morse.exe test_morse.c
 */
#include <stdio.h>
#include <string.h>

#define MORSE_HOST_TEST 1
#include "../main.c"

/* ------------------------------------------------------------ host shims */
static unsigned int sim_now;
static char outbuf[200000];
static size_t outlen;
static char inbuf[16384];
static int inlen;
static int inpos;
static int pin_state[64];
static int recorded_lamp;
static unsigned int change_time[4000];
static int change_state[4000];
static int change_count;

void host_uart_send(const char *text)
{
    while (*text != '\0' && outlen + 1 < sizeof(outbuf))
        outbuf[outlen++] = *text++;
    outbuf[outlen] = '\0';
}

int host_uart_read(char *byte)
{
    if (inpos >= inlen)
        return 0;
    *byte = inbuf[inpos++];
    return 1;
}

unsigned int host_now(void)
{
    return sim_now;
}

void gpio_write(int pin, int value)
{
    int i;
    int on = 1;

    if (pin < 0 || pin >= (int)(sizeof(pin_state) / sizeof(pin_state[0])))
        return;
    pin_state[pin] = value;

    for (i = 0; i < 8; ++i)
    {
        if (pin_state[led_pins[i]] != 0)
            on = 0;                 /* active low: any high pin means dark */
    }
    if (on != recorded_lamp)
    {
        recorded_lamp = on;
        if (change_count < (int)(sizeof(change_state) / sizeof(change_state[0])))
        {
            change_time[change_count] = sim_now;
            change_state[change_count] = on;
            change_count++;
        }
    }
}

int gpio_enable(int pin, int direction)
{
    (void)pin;
    (void)direction;
    return 0;
}

/* -------------------------------------------------------------- helpers */
static int checks;
static int failures;

#define CHECK(cond, text)                                                   \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            printf("FAIL line %d: %s\n", __LINE__, text);                   \
        }                                                                   \
    } while (0)

static void out_clear(void)
{
    outlen = 0;
    outbuf[0] = '\0';
}

static void feed(const char *text)
{
    while (*text != '\0' && inlen < (int)sizeof(inbuf) - 1)
        inbuf[inlen++] = *text++;
}

static void feed_clear(void)
{
    inlen = 0;
    inpos = 0;
}

static void reset_all(void)
{
    int i;
    sim_now = 0;
    for (i = 0; i < 64; ++i)
        pin_state[i] = 0;
    for (i = 0; i < 8; ++i)
        pin_state[led_pins[i]] = 1;
    recorded_lamp = 0;
    change_count = 0;
    feed_clear();
    out_clear();
    cmd_reset();
    out_clear();
}

static void run_ms(unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i)
    {
        sim_now++;
        morse_service(sim_now);
    }
}

static int count_events_on(int on)
{
    int i, n = 0;
    for (i = 0; i < event_count; ++i)
        if (events[i].on == on)
            n++;
    return n;
}

static unsigned int events_units(void)
{
    int i;
    unsigned int total = 0;
    for (i = 0; i < event_count; ++i)
        total += events[i].units;
    return total;
}

/* ----------------------------------------------------------------- tests */
static void test_morse_table(void)
{
    int i;

    CHECK(strcmp(morse_code('A'), ".-") == 0, "A is .-");
    CHECK(strcmp(morse_code('a'), ".-") == 0, "lowercase a is .-");
    CHECK(strcmp(morse_code('S'), "...") == 0, "S is ...");
    CHECK(strcmp(morse_code('O'), "---") == 0, "O is ---");
    CHECK(strcmp(morse_code('9'), "----.") == 0, "9 is ----.");
    CHECK(strcmp(morse_code('?'), "..--..") == 0, "? is ..--..");
    CHECK(morse_code('@') == NULL, "@ is not encodable");
    CHECK(morse_code(' ') == NULL, "space has no code");

    for (i = 0; i < MORSE_TABLE_SIZE; ++i)
    {
        const char *code = morse_table[i].code;
        size_t len = strlen(code);
        size_t k;
        CHECK(len >= 1 && len <= 6, "code length 1..6");
        for (k = 0; k < len; ++k)
            CHECK(code[k] == '.' || code[k] == '-', "only dots and dashes");
    }
}

static void test_encode_simple(void)
{
    int skipped, truncated;

    CHECK(morse_encode("E", &skipped, &truncated) == 1, "E encoded");
    CHECK(skipped == 0 && truncated == 0, "E no skip");
    CHECK(event_count == 1, "E is one event");
    CHECK(events[0].on == 1 && events[0].units == 1, "E is a dot");

    CHECK(morse_encode("I", &skipped, &truncated) == 1, "I encoded");
    CHECK(event_count == 3, "I has three events");
    CHECK(events[0].on == 1 && events[0].units == 1, "I dot");
    CHECK(events[1].on == 0 && events[1].units == 1, "I intra gap");
    CHECK(events[2].on == 1 && events[2].units == 1, "I dot");

    CHECK(morse_encode("T", &skipped, &truncated) == 1, "T encoded");
    CHECK(events[0].on == 1 && events[0].units == 3, "T is a dash");
}

static void test_encode_sos(void)
{
    int skipped, truncated;

    CHECK(morse_encode("SOS", &skipped, &truncated) == 3, "SOS three chars");
    CHECK(skipped == 0 && truncated == 0, "SOS complete");
    CHECK(events_units() == 27, "SOS lasts 27 units");
    CHECK(count_events_on(1) == 9, "SOS has nine marks");
    CHECK(events[0].on == 1 && events[0].units == 1, "SOS starts with a dot");
    CHECK(events[event_count - 1].on == 1, "no trailing gap");
    CHECK(event_count == 17, "SOS has seventeen events");
}

static void test_word_gap(void)
{
    int skipped, truncated;

    CHECK(morse_encode("A B", &skipped, &truncated) == 2, "A B two chars");
    CHECK(events[3].on == 0 && events[3].units == 7, "word gap is seven units");

    CHECK(morse_encode("A  B", &skipped, &truncated) == 2, "double space");
    CHECK(events[3].on == 0 && events[3].units == 7, "double space stays seven");

    CHECK(morse_encode(" A", &skipped, &truncated) == 1, "leading space");
    CHECK(events[0].on == 1 && events[0].units == 1, "leading space no gap");
}

static void test_invalid_chars(void)
{
    int skipped, truncated;

    CHECK(morse_encode("A@B#!", &skipped, &truncated) == 2, "two valid chars");
    CHECK(skipped == 3, "three unsupported characters");
    CHECK(truncated == 0, "not truncated");

    CHECK(morse_encode("@@@", &skipped, &truncated) == 0, "no valid chars");
    CHECK(skipped == 3, "all skipped");
}

static void test_wpm(void)
{
    int skipped, truncated;

    wpm = 12;
    CHECK(unit_ms() == 100, "12 WPM is 100 ms");
    wpm = 5;
    CHECK(unit_ms() == 240, "5 WPM is 240 ms");
    wpm = 30;
    CHECK(unit_ms() == 40, "30 WPM is 40 ms");
    wpm = WPM_DEFAULT;

    CHECK(morse_encode("PARIS", &skipped, &truncated) == 5, "PARIS encoded");
    CHECK(events_units() == 43, "PARIS is 43 units");
}

static void test_transmit_e(void)
{
    reset_all();
    begin_transmission("E");
    CHECK(state == STATE_SENDING, "E starts sending");
    CHECK(recorded_lamp == 1, "lamp on during dot");
    CHECK(strstr(outbuf, "SEND 1 CHARS") != NULL, "SEND reported");

    run_ms(99);
    CHECK(recorded_lamp == 1, "dot still on at 99 ms");
    run_ms(1);
    CHECK(state == STATE_IDLE, "E finished at 100 ms");
    CHECK(recorded_lamp == 0, "lamp off after E");
    CHECK(strstr(outbuf, "DONE 1 CHARS IN 100 MS") != NULL, "DONE 100 ms");
}

static void test_transmit_sos_timeline(void)
{
    int i;
    static const struct { unsigned int t; int on; } expected[] = {
        { 0, 1 }, { 100, 0 }, { 200, 1 }, { 300, 0 }, { 400, 1 }, { 500, 0 },
        { 800, 1 }, { 1100, 0 }, { 1200, 1 }, { 1500, 0 }, { 1600, 1 },
        { 1900, 0 }, { 2200, 1 }, { 2300, 0 }, { 2400, 1 }, { 2500, 0 },
        { 2600, 1 }, { 2700, 0 }
    };

    reset_all();
    begin_transmission("SOS");
    run_ms(2800);

    CHECK(state == STATE_IDLE, "SOS finished");
    CHECK(strstr(outbuf, "DONE 3 CHARS IN 2700 MS") != NULL, "SOS 2700 ms");

    CHECK(change_count == (int)(sizeof(expected) / sizeof(expected[0])),
          "SOS has the expected number of edges");
    for (i = 0; i < change_count && i < (int)(sizeof(expected) / sizeof(expected[0])); ++i)
    {
        CHECK(change_time[i] == expected[i].t, "SOS edge time");
        CHECK(change_state[i] == expected[i].on, "SOS edge level");
    }
}

static void test_speed_changes_timeline(void)
{
    reset_all();
    parse_command("SPEED 10");
    CHECK(wpm == 10, "SPEED 10 accepted");
    CHECK(strstr(outbuf, "OK 10 WPM 120 MS") != NULL, "speed reply");

    out_clear();
    parse_command("SPEED 4");
    CHECK(wpm == 10, "SPEED 4 rejected");
    CHECK(strstr(outbuf, "ERR SPEED") != NULL, "speed error");
    out_clear();
    parse_command("SPEED 31");
    CHECK(wpm == 10, "SPEED 31 rejected");
    out_clear();
    parse_command("SPEED 12x");
    CHECK(wpm == 10, "trailing junk rejected");
    out_clear();
    parse_command("SPEED");
    CHECK(wpm == 10, "missing value rejected");
}

static void test_stop_while_sending(void)
{
    reset_all();
    parse_command("MSG PARIS PARIS PARIS");
    CHECK(state == STATE_SENDING, "long message sending");
    run_ms(450);
    out_clear();
    feed("STOP\r\n");
    poll_uart();
    CHECK(state == STATE_IDLE, "STOP accepted while sending");
    CHECK(recorded_lamp == 0, "STOP darkens the lamp");
    CHECK(strstr(outbuf, "STOPPED") != NULL, "STOPPED reported");

    out_clear();
    parse_command("REPEAT");
    CHECK(state == STATE_SENDING, "REPEAT after STOP resends");
    CHECK(strstr(outbuf, "SEND 15 CHARS") != NULL, "REPEAT reports length");
    cmd_stop();
}

static void test_queue_message(void)
{
    reset_all();
    parse_command("MSG E");
    out_clear();
    parse_command("MSG SO");
    CHECK(strstr(outbuf, "QUEUED") != NULL, "second message queued");
    CHECK(strcmp(pending, "SO") == 0, "pending text stored");

    run_ms(100);
    CHECK(state == STATE_SENDING, "queued message starts automatically");
    CHECK(strcmp(message, "SO") == 0, "queued message becomes current");
    CHECK(strstr(outbuf, "NEXT") != NULL, "NEXT reported");
    CHECK(strstr(outbuf, "SEND 2 CHARS") != NULL, "queued SEND reported");
    run_ms(1900);
    CHECK(state == STATE_IDLE, "queued message finished");
}

static void test_repeat_and_reset(void)
{
    reset_all();
    out_clear();
    parse_command("REPEAT");
    CHECK(strstr(outbuf, "ERR NO MESSAGE") != NULL, "REPEAT without message");

    out_clear();
    parse_command("MSG E");
    run_ms(100);
    out_clear();
    parse_command("REPEAT");
    CHECK(state == STATE_SENDING, "REPEAT restarts last message");
    run_ms(100);
    CHECK(state == STATE_IDLE, "repeat completed");

    out_clear();
    parse_command("SPEED 20");
    parse_command("MSG SOS");
    run_ms(500);
    out_clear();
    parse_command("RESET");
    CHECK(state == STATE_IDLE, "RESET stops");
    CHECK(wpm == WPM_DEFAULT, "RESET restores 12 WPM");
    CHECK(message[0] == '\0', "RESET clears message");
    CHECK(recorded_lamp == 0, "RESET darkens lamp");
    CHECK(strstr(outbuf, "RESET OK") != NULL, "RESET reported");
}

static void test_parser(void)
{
    reset_all();

    feed("spe");
    poll_uart();
    CHECK(wpm == WPM_DEFAULT, "partial command not executed");
    feed("ed 15\r");
    poll_uart();
    CHECK(wpm == 15, "fragmented command assembled");

    feed("STATUS\n");
    poll_uart();
    CHECK(strstr(outbuf, "WPM=15") != NULL, "STATUS reports speed");

    out_clear();
    feed("FOO\r\n");
    poll_uart();
    CHECK(strstr(outbuf, "ERR") != NULL, "unknown command rejected");

    out_clear();
    {
        char longline[80];
        int i;
        for (i = 0; i < 60; ++i)
            longline[i] = 'A';
        longline[60] = '\r';
        longline[61] = '\n';
        longline[62] = '\0';
        feed(longline);
        poll_uart();
        poll_uart();
        CHECK(strstr(outbuf, "ERR LINE TOO LONG") != NULL, "overlong line rejected");
        CHECK(state == STATE_IDLE, "overlong line does not transmit");
    }

    out_clear();
    feed("MSG @@@\r");
    poll_uart();
    CHECK(strstr(outbuf, "ERR NO ENCODABLE") != NULL, "unencodable message");

    out_clear();
    feed("MSG \r");
    poll_uart();
    CHECK(strstr(outbuf, "ERR EMPTY MESSAGE") != NULL, "empty message");

    out_clear();
    feed("HELP\r");
    poll_uart();
    CHECK(strstr(outbuf, "COMMANDS: MSG") != NULL, "HELP text");

    out_clear();
    feed("   STATUS   \r");
    poll_uart();
    CHECK(strstr(outbuf, "STATUS IDLE") != NULL, "blank padding tolerated");
}

static void test_status_progress(void)
{
    reset_all();
    parse_command("MSG SOS");
    run_ms(650);
    out_clear();
    parse_command("STATUS");
    CHECK(strstr(outbuf, "STATUS SENDING") != NULL, "status sending");
    CHECK(strstr(outbuf, "EVENT=") != NULL, "status progress");
    CHECK(strstr(outbuf, "MSG=SOS") != NULL, "status message");
}

static void test_max_message(void)
{
    int skipped, truncated;
    char text[MORSE_MAX_MSG + 1];
    int i;

    for (i = 0; i < MORSE_MAX_MSG; ++i)
        text[i] = (i % 2 == 0) ? '5' : ' ';
    text[MORSE_MAX_MSG] = '\0';

    CHECK(morse_encode(text, &skipped, &truncated) == MORSE_MAX_MSG / 2,
          "40-character message encoded");
    CHECK(event_count <= MORSE_MAX_EVENTS, "event buffer respected");
    CHECK(truncated == 0, "worst case is not truncated");
}

int main(void)
{
    test_morse_table();
    test_encode_simple();
    test_encode_sos();
    test_word_gap();
    test_invalid_chars();
    test_wpm();
    test_transmit_e();
    test_transmit_sos_timeline();
    test_speed_changes_timeline();
    test_stop_while_sending();
    test_queue_message();
    test_repeat_and_reset();
    test_parser();
    test_status_progress();
    test_max_message();

    printf("%d checks, %d failures\n", checks, failures);
    printf(failures == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return failures == 0 ? 0 : 1;
}
