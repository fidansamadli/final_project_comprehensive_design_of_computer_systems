# Classroom demonstration script (about four minutes)

## Preparation

1. Board powered from its 5 V adapter; LED module wired to GPIO 9, 11, 15, 17,
   20, 22, 6, 7; module VCC on 3.3 V and GND on GND.
2. Serial cable on UART5, terminal at 115200 8N1.
3. `MorseMessenger.exe` built and downloaded to RAM in LoongIDE (Download by
   network off).

## Script

| Step | Action | Expected observation |
|---|---|---|
| 1 | Start the downloaded program in LoongIDE | `MORSE MESSENGER READY` and `TYPE HELP FOR COMMANDS` on the terminal; all LEDs off. |
| 2 | Type `HELP` | The command list followed by `EXAMPLE: MSG SOS`. |
| 3 | Type `MSG SOS` | `SEND 3 CHARS AT 12 WPM`; the LED module flashes three short, three long, three short. |
| 4 | Wait | `DONE 3 CHARS IN 2700 MS`; LEDs off. |
| 5 | Type `SPEED 20` | `OK 20 WPM 60 MS`. |
| 6 | Type `MSG HELLO WORLD` | Faster transmission; a seven-unit pause is visible between the two words. |
| 7 | Type `STOP` during the message | `STOPPED` appears immediately, not after the message ends; LEDs go dark. This demonstrates that the parser is not blocked by the transmitter. |
| 8 | Type `REPEAT` | The last message is sent again from the beginning. |
| 9 | Type `MSG A1B2` then `MSG SOS` while the first is playing | `QUEUED`, then `NEXT` and the second message starts automatically. |
| 10 | Type `MSG A@B` | `SEND 2 CHARS AT 20 WPM, SKIP 1`; the unsupported character is reported and skipped. |
| 11 | Type `STATUS` | `STATUS IDLE WPM=20 UNIT=60MS MSG=A@B`. |
| 12 | Type `RESET` | `RESET OK`, `MORSE MESSENGER READY`; speed back to 12 WPM. |

## Points to explain while demonstrating

* The dot duration is 1200 / WPM; at 12 WPM one unit is 100 ms, so SOS lasts
  27 units = 2700 ms.
* The LEDs are active low, so a mark writes 0 to all eight GPIO pins.
* The timeline is driven by the RT-Thread tick, not by `delay()` calls, which is
  why `STOP` is accepted in the middle of a dash.
* The command parser and the transmitter share the main loop; each pass reads a
  bounded number of UART bytes so a continuous input stream cannot starve the
  Morse timing.

## If the board is unavailable

Run `tests\run_tests.ps1` and show the passing terminal output, then explain the
waveform test: it checks every LED edge of `SOS` against the expected times
(0, 100, 200, 300, 400, 500, 800, 1100, ... 2700 ms). The same `main.c` is used
for the board build.
