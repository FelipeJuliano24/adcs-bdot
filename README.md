<h1 align="center">
    ADCS Bdot FIRMWARE PROJECT
    <br>
</h1>

<h4 align="center">Firmware project of the ADCS `Bdot Version` module.</h4>

<p align="center">
    <a href="#overview">Overview</a> •
    <a href="#dependencies">Dependencies</a> •
    <a href="#compiling-and-building">Compiling and building</a> •
    <a href="#flashing">Flashing</a>
</p>

## Overview

The product tree of the firmware can be seen below:

### UART telemetry and telecommands

The OBDH link uses the STM32F4 USART3 at **115200 bit/s, 8N1**, on
`PD8/TX` and `PD9/RX`. This UART is reserved exclusively for PUS: do not send
`printk`, shell, or debug-console output through it after the link is started.
Use a separate physical debug interface in the flight harness.

Cross-connect `PD8` to the OBDH RX and `PD9` to the OBDH TX, share ground, and
verify compatible logic levels before integration. The defaults can be changed
at build time with `OBDH_UART_BAUD_RATE`, `OBDH_UART_TX_GPIO_PORT/PIN`, and
`OBDH_UART_RX_GPIO_PORT/PIN`; alternate pins must still be valid USART3 AF7
pins on the selected STM32F4 package.

Each raw PUS frame (`APID | length | service | subtype | data | CRC`) is
transported between `0x7e` flag bytes. Values `0x7e` and `0x7d` within a frame
are escaped as `0x7d` followed by the byte XOR `0x20`. The OBDH must use the
same framing in both directions. This lets the receiver re-synchronise after
a partial or corrupted serial frame instead of guessing PUS packet boundaries.

RX is interrupt-driven: the USART ISR only moves bytes into a 2048-byte
single-producer/single-consumer ring and signals the reports task with an
RTEMS event. HDLC decoding, CRC validation, PUS dispatch, and ACK generation
run in task context; the application never polls the UART receive register.
Hardware line errors and ring overflow are emitted as
`ADCS_TM_UART_LINK_ERROR` telemetry. Malformed framed input is reported as
`ADCS_TM_PARSER_ERROR` with `PUS_PARSE_ERROR_TRANSPORT`.

PUS command service (`8`) subtype `3` controls B-dot: one data byte, `0` to
disable (torquer command is set to zero) and `1` to enable.

```text
rtems_project/
├── patches/            # Custom .patch files for the kernel
├── rtems-source/       # Original source code cloned from RTEMS
└── build_dir/          # Output directory for the compiled firmware
```

Compiling this RTEMS-based firmware requires applying patches to the base kernel to add support for specific hardware (BSPs) or to modify real-time behaviors before building.

## Dependencies

* RTEMS Project Toolchain
* st-link tools

### Installation on Ubuntu

```bash
sudo apt install gcc-arm-none-eabi stlink-tools
```

### Installation on Fedora

```bash
sudo dnf install gcc-arm-linux-gnu stlink
```

## Compiling and building

The build process uses strictly relative paths. Follow the sequence below to download the source, apply patches, and compile:

**1. Download the RTEMS Source Code**
```bash
git clone git://git.rtems.org/rtems.git rtems-source
```

**2. Navigate to the Source Tree**
```bash
cd rtems-source
```

**3. Apply the Patch to the Kernel**
Apply the patch fetching the file from the sibling directory:
```bash
patch -p1 < ../patches/custom_bsp_fix.patch
```

**4. Configure the Build Environment**
Initialize the build context using the Waf build system targeting the STM32F4 BSP:
```bash
./waf configure --prefix=../build_dir --rtems-bsp=arm/stm32f4 --rtems-tools=../../opt/rtems/6
```

**5. Compile and Install**
Build the modified kernel and install the final artifacts into the output directory:
```bash
./waf build
./waf install
```

## Flashing

```bash
make flash
```

## Orbit prediction

`libpredict` is compiled into the RTEMS application. The `orbit_prediction_task`
propagates the configured TLE every 30 seconds, stores the sub-satellite
latitude and longitude, and sends the unconsumed `RTEMS_EVENT_1` event once on
each entry into the Brazil geofence. It does not command any subsystem.

Configure the mission TLE during CMake configuration; do not use an unrelated
example TLE for flight:

```bash
cmake -S . -B out \
  -DORBIT_PREDICTION_TLE_LINE_1='<mission TLE line 1>' \
  -DORBIT_PREDICTION_TLE_LINE_2='<mission TLE line 2>'
```

The RTEMS clock must first be set to valid UTC (for example, by GPS) or the
prediction reports `INVALID_TIME`. If no valid TLE is configured, it reports
`INVALID_TLE` and publishes zero coordinates instead of a fictitious position.

Every housekeeping payload now ends with a 9-byte orbit record: prediction
status (1 byte), latitude in degrees (IEEE-754 `float`, 4 bytes), and longitude
in degrees (IEEE-754 `float`, 4 bytes).
