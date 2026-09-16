# Wireless Optical Mouse — nRF52

A wireless optical mouse prototype built with Nordic Semiconductor nRF52 devices, a PMW3389 optical sensor, Enhanced ShockBurst (ESB) wireless communication, and USB HID.

The system is split into two embedded devices:

- **PTX:** nRF52833 PCA10100 development kit
- **PRX:** nRF52840 PCA10059 USB dongle
- **Sensor:** PixArt PMW3389
- **Wireless link:** Nordic Enhanced ShockBurst at 2 Mbps
- **Host interface:** USB HID mouse

## System Architecture

```text
PMW3389 + buttons
        |
        v
nRF52833 PTX
        |
        |  2.4 GHz ESB
        v
nRF52840 PRX
        |
        |  USB HID
        v
      Host PC
```

## Key Design Features

### PTX

- PMW3389 motion acquisition over SPI
- 2 kHz active sensor scheduling
- approximately 100 ms idle polling after inactivity
- signed 32-bit motion accumulation
- signed 16-bit motion values in each wireless packet
- one application packet in flight at a time
- ESB acknowledgments used for delivery feedback
- automatic ESB retransmission disabled
- motion committed only after successful transmission
- sensor acquisition continues while a radio packet is in flight
- asynchronous button input through GPIOTE

### PRX

- receives and validates 6-byte wireless mouse packets
- accumulates received motion in signed 32-bit pending values
- converts wireless input into a 6-byte USB HID mouse report
- signed 16-bit X/Y HID motion
- 1 ms USB interrupt-IN polling interval
- motion committed only after successful USB transfer
- USB suspend/resume handling
- button-triggered USB remote wake

## Wireless Packet Format

The PTX and PRX exchange a fixed 6-byte application payload:

| Offset | Field | Type | Description |
|---|---|---|---|
| 0 | `packet_sequence` | `uint8_t` | Diagnostic sequence number |
| 1 | `buttons` | `uint8_t` | Current button state |
| 2–3 | `x` | `int16_t` | Relative X-axis motion |
| 4–5 | `y` | `int16_t` | Relative Y-axis motion |

The sequence number is retained primarily for diagnostics. Normal mouse operation does not depend on it.

## Motion Preservation

Both the transmitter and receiver use a snapshot-and-commit model.

On the PTX, measured motion is accumulated before transmission. A snapshot is submitted to ESB, but the corresponding motion is removed from the accumulator only after the radio reports successful delivery.

On the PRX, received motion is accumulated independently of USB timing. A HID report snapshot is queued when endpoint IN1 is available and is committed only after the USB transfer completes successfully.

This keeps the sensor, wireless link, and USB interface independently scheduled while preserving pending relative motion.

## Timing

| Function | Rate / Interval |
|---|---|
| PTX active sensor scheduling | 500 µs / 2 kHz |
| PTX idle polling | ~100 ms |
| PTX idle timeout | ~10 s |
| ESB bitrate | 2 Mbps |
| USB HID polling interval | 1 ms / 1 kHz |

## Validation

Functional testing confirmed:

- X/Y cursor movement
- slow and fast motion
- abrupt stops without visible residual movement
- left-button dragging and release
- right-click operation
- simultaneous motion and button input
- PTX idle entry and wake
- USB suspend/resume behavior
- button-triggered USB remote wake on a host that maintained USB power during suspend

Firmware instrumentation also verified approximately 2 kHz active sensor acquisition and correct PTX motion-accounting behavior during the tested conditions.

The measured results are intended as implementation validation rather than controlled latency, RF reliability, power-consumption, or maximum-throughput measurements.

## Documentation

Detailed firmware documentation is available here:

- [PTX Firmware Documentation](documents/PTX-documentation.pdf)
- [PRX Firmware Documentation](documents/PRX-documentation.pdf)

## Development Environment

- Nordic nRF5 SDK 17.1.0
- SEGGER Embedded Studio
- Embedded C
- Nordic Enhanced ShockBurst
- Nordic USB device driver / USB HID

## Project Status

The current prototype implements the complete motion path from optical sensor acquisition through wireless transport and USB HID reporting, including active/idle scheduling, motion preservation, button input, USB suspend/resume, and remote wake.