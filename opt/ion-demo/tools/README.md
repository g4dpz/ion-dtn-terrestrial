# Tools

## Wireshark KISS/AX.25/LTP Dissector

`kiss_ltp_dissector.lua` is a Wireshark Lua plugin that decodes the KISS → AX.25 → LTP protocol stack used by `ionserialcla`.

### Installation

Copy the dissector to your Wireshark personal plugins folder:

```bash
# macOS
mkdir -p ~/.local/lib/wireshark/plugins/
cp kiss_ltp_dissector.lua ~/.local/lib/wireshark/plugins/

# Linux
mkdir -p ~/.local/lib/wireshark/plugins/
cp kiss_ltp_dissector.lua ~/.local/lib/wireshark/plugins/
```

Or find the folder via Wireshark: Help > About Wireshark > Folders > Personal Lua Plugins.

Restart Wireshark after copying.

### Capturing Serial Traffic

#### Option 1: Capture with socat

Mirror the serial port to a hex dump:

```bash
socat -x /dev/tty.usbmodemXXX,raw,echo=0,b9600 STDOUT 2>&1 | tee capture.hex
```

Convert to pcap:

```bash
text2pcap -l 147 capture.hex capture.pcap
```

Open `capture.pcap` in Wireshark. The dissector auto-registers on DLT 147 (USER0).

#### Option 2: Parse ion.log hex dumps

Extract hex frames from the debug log:

```bash
grep "kiss_send KISS frame\|RX raw serial" /tmp/ion_node_a/ion.log
```

The hex bytes in the debug output can be manually converted to a pcap using `text2pcap`.

### What It Decodes

The dissector shows three protocol layers:

- **KISS**: Frame delimiters (FEND), command type (Data, TX Delay, TX Tail, etc.)
- **AX.25**: Source and destination callsigns with SSIDs, control field, PID
- **LTP**: Segment type (Red Data, Checkpoint, Report, Report Ack, Cancel), session ID, payload

Example decoded output:

```
KISS Frame
  Frame End: 0xc0
  Port: 0
  Command Type: 0x00 (Data Frame)
AX.25 UI Frame
  Destination: G4DPZ-2
  Source: G4DPZ-1
  Control: 0x03
  PID: 0xf0
LTP Segment
  Version: 0
  Segment Type: 0x03 (Red Data (checkpoint + EORP))
  Payload: [64 bytes]
```
