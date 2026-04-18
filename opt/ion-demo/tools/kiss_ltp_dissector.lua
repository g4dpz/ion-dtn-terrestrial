-- kiss_ltp_dissector.lua
-- Wireshark Lua dissector for KISS / AX.25 / LTP frames
-- as used by ionserialcla over serial packet radio
--
-- Installation:
--   Copy this file to your Wireshark personal Lua plugins folder:
--     macOS:  ~/.local/lib/wireshark/plugins/
--     Linux:  ~/.local/lib/wireshark/plugins/
--     Windows: %APPDATA%\Wireshark\plugins\
--   Or find the folder via: Help > About Wireshark > Folders > Personal Lua Plugins
--   Restart Wireshark after copying.
--
-- Capturing serial data:
--   Use socat or interceptty to mirror serial port traffic to a file,
--   then import as raw data. Or use the "User DLT" approach:
--
--   1. Capture with: socat -x /dev/tty.usbmodemXXX,raw,echo=0 - 2>&1 | tee capture.hex
--   2. Convert to pcap using text2pcap:
--      text2pcap -l 147 capture.hex capture.pcap
--   3. Open capture.pcap in Wireshark
--   4. Set DLT 147 to use this dissector:
--      Edit > Preferences > Protocols > DLT_USER > User 0 (DLT=147) > kiss_ltp
--
-- Alternatively, for live capture from ion.log debug hex dumps:
--   grep "RX raw serial\|TX AX.25 frame\|kiss_send KISS frame" ion.log > frames.txt
--   Then parse the hex manually.

-- KISS protocol
local kiss_proto = Proto("kiss_serial", "KISS (Serial TNC)")

local kiss_fields = {
    fend     = ProtoField.uint8("kiss.fend", "Frame End", base.HEX),
    cmd      = ProtoField.uint8("kiss.cmd", "Command", base.HEX),
    port     = ProtoField.uint8("kiss.port", "Port", base.DEC),
    cmd_type = ProtoField.uint8("kiss.cmd_type", "Command Type", base.HEX),
    data     = ProtoField.bytes("kiss.data", "Data"),
}
kiss_proto.fields = kiss_fields

local kiss_cmd_types = {
    [0x00] = "Data Frame",
    [0x01] = "TX Delay",
    [0x02] = "Persistence",
    [0x03] = "Slot Time",
    [0x04] = "TX Tail",
    [0x05] = "Full Duplex",
    [0x06] = "Set Hardware",
    [0xFF] = "Return (exit KISS)",
}

-- AX.25 protocol
local ax25_proto = Proto("ax25_ui", "AX.25 UI Frame")

local ax25_fields = {
    dst_call = ProtoField.string("ax25.dst", "Destination"),
    src_call = ProtoField.string("ax25.src", "Source"),
    dst_ssid = ProtoField.uint8("ax25.dst_ssid", "Dst SSID", base.DEC),
    src_ssid = ProtoField.uint8("ax25.src_ssid", "Src SSID", base.DEC),
    control  = ProtoField.uint8("ax25.control", "Control", base.HEX),
    pid      = ProtoField.uint8("ax25.pid", "PID", base.HEX),
    info     = ProtoField.bytes("ax25.info", "Info Field"),
}
ax25_proto.fields = ax25_fields

-- LTP protocol
local ltp_proto = Proto("ltp_serial", "LTP (over AX.25/KISS)")

local ltp_fields = {
    version    = ProtoField.uint8("ltp.version", "Version", base.DEC),
    type_code  = ProtoField.uint8("ltp.type", "Segment Type", base.HEX),
    session_id = ProtoField.bytes("ltp.session_id", "Session ID"),
    payload    = ProtoField.bytes("ltp.payload", "Payload"),
}
ltp_proto.fields = ltp_fields

local ltp_types = {
    [0x00] = "Red Data",
    [0x01] = "Red Data (checkpoint)",
    [0x02] = "Red Data (EORP)",
    [0x03] = "Red Data (checkpoint + EORP)",
    [0x04] = "Green Data",
    [0x05] = "Green Data (undefined)",
    [0x06] = "Green Data (undefined)",
    [0x07] = "Green Data (EOB)",
    [0x08] = "Report Segment",
    [0x09] = "Report Ack",
    [0x0A] = "Reserved",
    [0x0B] = "Reserved",
    [0x0C] = "Cancel (sender)",
    [0x0D] = "Cancel Ack (sender)",
    [0x0E] = "Cancel (receiver)",
    [0x0F] = "Cancel Ack (receiver)",
}

-- Helper: decode AX.25 callsign from 7 shifted bytes
local function decode_callsign(tvb, offset)
    local call = ""
    for i = 0, 5 do
        local ch = bit.rshift(tvb(offset + i, 1):uint(), 1)
        if ch > 0x20 then
            call = call .. string.char(ch)
        end
    end
    call = call:gsub("%s+$", "") -- trim trailing spaces
    local ssid_byte = tvb(offset + 6, 1):uint()
    local ssid = bit.band(bit.rshift(ssid_byte, 1), 0x0F)
    if ssid > 0 then
        call = call .. "-" .. tostring(ssid)
    end
    return call, ssid
end

-- KISS dissector
function kiss_proto.dissector(tvb, pinfo, tree)
    local length = tvb:len()
    if length < 3 then return end

    pinfo.cols.protocol = "KISS"

    local subtree = tree:add(kiss_proto, tvb(), "KISS Frame")

    -- Check for FEND at start
    local first = tvb(0, 1):uint()
    local offset = 0
    if first == 0xC0 then
        subtree:add(kiss_fields.fend, tvb(0, 1))
        offset = 1
    end

    if offset < length then
        local cmd = tvb(offset, 1):uint()
        local port = bit.rshift(cmd, 4)
        local cmd_type = bit.band(cmd, 0x0F)

        subtree:add(kiss_fields.port, port)
        local cmd_item = subtree:add(kiss_fields.cmd_type, tvb(offset, 1))
        cmd_item:append_text(" (" .. (kiss_cmd_types[cmd_type] or "Unknown") .. ")")
        offset = offset + 1

        if cmd_type == 0x00 and offset < length then
            -- Data frame — pass to AX.25 dissector
            -- Find end FEND
            local data_end = length
            if tvb(length - 1, 1):uint() == 0xC0 then
                data_end = length - 1
            end

            local data_len = data_end - offset
            if data_len > 0 then
                local data_tvb = tvb(offset, data_len):tvb()
                ax25_proto.dissector(data_tvb, pinfo, tree)
            end
        end
    end
end

-- AX.25 dissector
function ax25_proto.dissector(tvb, pinfo, tree)
    local length = tvb:len()
    if length < 16 then return end

    pinfo.cols.protocol = "AX.25"

    local subtree = tree:add(ax25_proto, tvb(), "AX.25 UI Frame")

    local dst_call, dst_ssid = decode_callsign(tvb, 0)
    local src_call, src_ssid = decode_callsign(tvb, 7)

    subtree:add(ax25_fields.dst_call, tvb(0, 7), dst_call)
    subtree:add(ax25_fields.src_call, tvb(7, 7), src_call)
    subtree:add(ax25_fields.control, tvb(14, 1))
    subtree:add(ax25_fields.pid, tvb(15, 1))

    pinfo.cols.info = src_call .. " → " .. dst_call

    if length > 16 then
        local info_tvb = tvb(16, length - 16):tvb()
        ltp_proto.dissector(info_tvb, pinfo, tree)
    end
end

-- LTP dissector
function ltp_proto.dissector(tvb, pinfo, tree)
    local length = tvb:len()
    if length < 1 then return end

    pinfo.cols.protocol = "LTP"

    local subtree = tree:add(ltp_proto, tvb(), "LTP Segment")

    local first_byte = tvb(0, 1):uint()
    local version = bit.rshift(first_byte, 4)
    local type_code = bit.band(first_byte, 0x0F)

    subtree:add(ltp_proto.fields.version, version)
    local type_item = subtree:add(ltp_proto.fields.type_code, tvb(0, 1))
    type_item:append_text(" (" .. (ltp_types[type_code] or "Unknown") .. ")")

    pinfo.cols.info:append(" | LTP " .. (ltp_types[type_code] or "Unknown") .. " (" .. length .. " bytes)")

    if length > 1 then
        subtree:add(ltp_proto.fields.payload, tvb(1, length - 1))
    end
end

-- Register for DLT_USER0 (147)
local wtap_encap_table = DissectorTable.get("wtap_encap")
wtap_encap_table:add(wtap.USER0, kiss_proto)
