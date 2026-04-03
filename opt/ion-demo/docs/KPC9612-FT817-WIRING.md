# KPC-9612+ to FT-817 Wiring Guide for 9600 Baud

## Overview

This guide describes how to connect a Kantronics KPC-9612+ TNC to a Yaesu FT-817 for 9600 baud FSK packet radio operation with the DTN demo system.

## Important

Verify all connections against your specific hardware manuals before wiring. The KPC-9612+ has multiple radio port connectors — use the correct one for your baud rate.

## Connectors

### KPC-9612+ Radio Port 2 (9600 baud)

The 9600 baud radio port on the KPC-9612+ rear panel is a DB-15 female connector. Key pins for 9600 baud operation:

```
KPC-9612+ DB-15 (Radio Port 2 - 9600 baud)
Pin 1:  TX Audio Out (to radio TX audio in)
Pin 2:  Ground
Pin 3:  PTT (active low — ground to transmit)
Pin 4:  RX Audio In (from radio RX audio out)
Pin 9:  Ground
```

Note: Consult the KPC-9612+ manual (Chapter 7) for the complete pinout. The DB-15 pin assignments may vary by hardware revision.

### FT-817 DATA Port

The FT-817 rear panel DATA port is a 6-pin mini-DIN female connector:

```
FT-817 6-pin Mini-DIN DATA Port (view from rear panel)

Pin 1:  TX Audio In (DATA IN) — 9600: direct to modulator
Pin 2:  Ground
Pin 3:  PTT (ground to transmit)
Pin 4:  RX Audio Out (unprocessed — 9600 baud discriminator output)
Pin 5:  RX Audio Out (processed — 1200 baud, filtered)
Pin 6:  Squelch logic
```

For 9600 baud, use Pin 4 (unprocessed discriminator output), not Pin 5.

## Wiring

```
KPC-9612+ DB-15          FT-817 6-pin Mini-DIN
(Radio Port 2)           (DATA port, rear panel)
─────────────────────────────────────────────────
Pin 1 (TX Audio Out) ──→ Pin 1 (TX Audio In)
Pin 4 (RX Audio In)  ←── Pin 4 (RX Audio Out, 9600)
Pin 3 (PTT)          ──→ Pin 3 (PTT)
Pin 2 (Ground)       ──→ Pin 2 (Ground)
```

## Cable Construction

You need:
- DB-15 male connector (for KPC-9612+ radio port)
- 6-pin mini-DIN male connector (for FT-817 DATA port)
- Shielded audio cable (4 conductors + shield)

```
Wire    From (DB-15)    To (Mini-DIN)    Signal
──────────────────────────────────────────────────
Red     Pin 1           Pin 1            TX Audio
Orange  Pin 4           Pin 4            RX Audio (9600)
Yellow  Pin 3           Pin 3            PTT
Black   Pin 2           Pin 2            Ground
Shield  Pin 9           Pin 2            Ground
```

Keep the cable short (under 1 meter) to minimize audio signal degradation.

## FT-817 Configuration for 9600 Baud

1. Press FUNC then SET to enter menu mode
2. Menu 24 (PKT RATE): Set to 9600
3. Menu 14 (DIG MODE): Set to USER-U
4. Press FUNC then SET to exit

The 9600 baud setting routes audio through Pin 4 (unprocessed discriminator) for receive, and accepts direct modulator input on Pin 1 for transmit.

## KPC-9612+ Configuration

Enter command mode (press * * * then Enter):

```
RESET              (factory reset — optional)
INTFACE KISS       (enable KISS mode for DTN CLA)
HBAUD 9600         (host serial port baud rate)
```

For the host (computer/STM32) serial connection, use the KPC-9612+ RS-232 port (DB-9 or DB-25 depending on model).

## Audio Level Adjustment

9600 baud FSK is more sensitive to audio levels than 1200 baud AFSK:

1. Start with the KPC-9612+ TX audio level at mid-range
2. Monitor the FT-817 ALC meter during transmission — it should show minimal deflection
3. If the FT-817 shows high ALC, reduce the KPC-9612+ TX level
4. For receive, the discriminator output (Pin 4) is typically at the right level without adjustment

## Testing

Once wired and configured:

```bash
# On the host computer connected to KPC-9612+ RS-232 port:
./opt/ion-demo/scripts/start_node_a.sh /dev/ttyUSB0
```

The KISS protocol and CLA code are identical to the Mobilinkd TNC3 setup — only the physical connection and baud rate change.

## Comparison: Mobilinkd TNC3 vs KPC-9612+

| Aspect | Mobilinkd TNC3 | KPC-9612+ |
|--------|----------------|-----------|
| Interface | USB (CDC) | RS-232 (DB-9/DB-25) |
| Baud rates | 1200 only | 1200 + 9600 |
| Radio connection | 3.5mm audio | DB-15 |
| KISS mode | Default | Command: INTFACE KISS |
| STM32 connection | Needs USB host | Direct UART via MAX3232 |
| Power | USB bus powered | External 12V DC |

## References

- Kantronics KPC-9612+ Getting Started and Reference Manual
- Yaesu FT-817 Operating Manual (DATA port pinout)
- Content was rephrased for compliance with licensing restrictions
