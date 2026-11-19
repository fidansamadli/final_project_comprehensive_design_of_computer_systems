# LED Morse Code Messenger

Final project for **Computer Systems Comprehensive Design** (计算机系统综合设计).

The Loongson 1B board receives a short text message on its serial port, converts
each character into International Morse Code and transmits it on the eight-LED
module of the course kit. Dots are 1 time unit of light, dashes 3 units, the
gap inside a character 1 unit, between characters 3 units and between words
7 units. The speed is selected in words per minute (one unit = 1200 / WPM ms).

The application is a single C file that reuses the RT-Thread 4.0.2 board support
package, the UART5 driver and the active-low GPIO helpers already used in
Experiment 6. No additional hardware is required.

## Hardware

| Item | Connection |
|---|---|
| Loongson 1B board | 1B-Board V1.0 / 1B-Core V1.0, 5 V adapter |
| LED module | LED1..LED8 to GPIO 9, 11, 15, 17, 20, 22, 6, 7 |
| LED module VCC | 3.3 V pin of the expansion header |
| LED module GND | GND pin of the expansion header |
| Serial console | UART5, 115200 baud, 8 data bits, no parity, 1 stop bit |
| Debug probe | LxLink EJTAG, RAM download only |

The LEDs are active low: writing 0 to a GPIO pin lights the LED. All eight LEDs
form one lamp, so a mark is visible across the module.

The Morse parser uses the board console (UART5). The generic RT-Thread FinSH
shell is disabled so it cannot consume Morse command characters. Keep PuTTY
on the working console cable at 115200 8N1, with no flow control.

## Commands

Commands are terminated by CR, LF or CRLF. Letter case, surrounding spaces and
occasional backspaces are tolerated.

| Command | Effect |
|---|---|
| `MSG <text>` | Encode and transmit `<text>` (up to 40 characters). If a message is already being sent, the new one is queued and starts automatically. |
| `REPEAT` | Transmit the last message again. Rejected while a message is being sent. |
| `SPEED <wpm>` | Set 5..30 words per minute (240..40 ms per unit). Applies from the next element. |
| `STOP` | Abort the transmission, clear the queue and switch the LEDs off. |
| `RESET` | Stop, forget the message and restore 12 WPM. |
| `STATUS` | Report state, speed, message, pending flag and event progress. |
| `HELP` | Print the command list. |

Replies: `SEND n CHARS AT w WPM`, `DONE n CHARS IN t MS`, `QUEUED`, `NEXT`,
`STOPPED`, `RESET OK`, `OK`, `ERR ...`, `STATUS ...`.

Characters without a Morse representation (for example `@`, `#`, `_`) are
skipped and counted in the `SEND` line (`SKIP n`). A message with no encodable
character is rejected. Lines longer than 47 characters are discarded and
answered with `ERR LINE TOO LONG`.

Example session:

```
MORSE MESSENGER READY
TYPE HELP FOR COMMANDS
MSG SOS
SEND 3 CHARS AT 12 WPM
DONE 3 CHARS IN 2700 MS
SPEED 20
OK 20 WPM 60 MS
MSG HELLO WORLD
SEND 10 CHARS AT 20 WPM
```

## Build

Requirements:

* SDE Lite GCC 4.9.2 for MIPS (`mips-sde-elf-*` tools)
* GNU Make (the MSYS build supplied with LoongIDE is suitable)
* Windows PowerShell 5.1 or newer

Set `MIPS_SDE_BIN` to the toolchain's `bin` directory. Set `MAKE_EXE` if
`make.exe` is not already on `PATH`.

```
$env:MIPS_SDE_BIN = 'C:\path\to\mips-2015.05\bin'
$env:MAKE_EXE = 'C:\path\to\make.exe' # optional when make.exe is on PATH
.\build.ps1
```

The build produces `Build\MorseMessenger.exe` (ELF32 little-endian MIPS,
O32, soft float, entry `0x80200000`) and `Build\MorseMessenger.bin`. Open
`MorseMessenger.lxp` for the LoongIDE workflow and select your local SDE Lite
compiler installation if LoongIDE asks for it.

## Host tests

```
gcc -std=c99 -Wall -Wextra -Werror -I. -o tests/test_morse.exe tests/test_morse.c
tests/test_morse.exe
```

or `tests\run_tests.ps1`. The tests compile `main.c` itself with
`MORSE_HOST_TEST`, so the encoder, state machine, command parser and UART line
assembler are the same code that is cross-compiled for the board. Only GPIO,
UART and the tick are replaced. The exhaustive checks include the complete
SOS LED waveform, the PARIS timing reference, speed limits, queueing, STOP
during transmission, malformed and overlong input.

## Running on the board

1. Connect the LED module and the serial cable (UART5) as listed above.
2. Open `MorseMessenger.lxp` in LoongIDE, confirm that *Download by network* is
   off and *Reset on stop* is disabled.
3. Build, then `Debug -> Run` to download the image to RAM. No flash operation
   is required.
4. Open the serial terminal at 115200 8N1 and type `MSG SOS`.
5. Type `STOP` while the message is playing to check that input stays
   responsive.

If the debugger cannot connect, the program can also be programmed through the
board's normal PMON download procedure; do not erase the flash to run this
demonstration.

## Verification

Run `tests\run_tests.ps1` for the host test suite. After a target build, run
`python tools\verify_artifact.py` to validate the ELF architecture, entry point,
RAM placement and required application symbols. Generated binaries, logs and
test results are intentionally excluded from version control.

## Files

```
main.c                     application (encoder, timing, parser)
MorseMessenger.lxp         LoongIDE project
Build/                     generated makefiles and local build output
tests/test_morse.c         host tests
tests/host_platform.h      host shim
tests/run_tests.ps1        host test driver
tools/verify_artifact.py   ELF/architecture verification
DEMO.md                    classroom demonstration script
```
