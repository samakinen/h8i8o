# The ETS application

What the device does on the KNX bus, and how ETS configures it. Application program
`M-00FA_A-0001-02` ("H8i8o 8 inputs 8 outputs", application number 1, version 2).

## Communication objects

| Number | Name | DPT | Flags | Behaviour |
|---|---|---|---|---|
| 0–7 | Input 1–8 | 1.001 Switch | C R T | Sends the input's state when it changes; answers reads |
| 8–15 | Output 1–8 switch | 1.001 Switch | C W | Switches DO1–DO8 |
| 16–23 | Output 1–8 status | 1.001 Switch | C R T | Sent after each switch; answers reads |

The device holds up to 128 group addresses and 160 associations.

## Parameters

One page per channel: "Input 1" to "Input 8" and "Output 1" to "Output 8".

| Page | Parameter | Values | Default | Shown |
|---|---|---|---|---|
| Input *n* | Function | Disabled / Switch | Switch | always |
| | Contact type | Normally open / Normally closed | Normally open | unless the function is Disabled |
| | Debounce time | 10, 20, 30, 50, 100, 200 ms | 30 ms | unless the function is Disabled |
| Output *n* | Channel | Disabled / Enabled | Enabled | always |
| | Operating mode | Normally open (1 = energise) / Normally closed (1 = de-energise) | Normally open | while the channel is Enabled |
| | Status object | Disabled / Send on change | Send on change | while the channel is Enabled |

New parameters take effect when the download completes, without a restart.

## Inputs DI1–DI8

- **Switch.** The input object follows the contact: 1 while the contact is closed, 0 while
  it is open. Each change is sent once, after the contact has been stable for the debounce
  time. A push button therefore sends 1 on press and 0 on release.
- **Normally closed** inverts the input: 1 while the contact is open.
- **Disabled.** The channel never sends, and its object reads 0.
- At start the device takes the current state of every input without sending it. Read the
  object to get it.
- Changes are sent only in normal operation. While programming mode is on, or before the
  device has been downloaded, the state is tracked but not sent.

An input is active when its pin is pulled to ground; see [hardware.md](hardware.md).

## Outputs DO1–DO8

- **Switch object.** 1 switches the channel on, 0 switches it off.
- **Operating mode.** "Normally open": on energises the output (pin high). "Normally
  closed": on releases it (pin low). Use it for a normally closed contact or an inverted
  load, so that the objects keep meaning "on" and "off" for the load.
- **Status object.** Reports the channel's state as the switch object means it (on or off,
  operating mode applied). With "Send on change" it is sent after every switch telegram,
  including one that does not change the state. With "Disabled" it is never sent but still
  answers reads.
- **Disabled channel.** Switch telegrams are ignored and the output stays released.
- **At start**, after a restart or a bus power cut, every output is released (pin low). The
  previous state is not restored, and no status is sent. A channel in "Normally closed" mode
  therefore reads as on after a start.

## Programming button and LED

- A press of the programming button toggles programming mode. Press it **once** when ETS
  asks: a second press ends programming mode before ETS has written the address.
- The LED is on while programming mode is on, and off otherwise.
- Programming mode stays on until the button is pressed again or the device restarts; it has
  no timeout.

## Commissioning with ETS6

1. Build, sign and import the product file ([tools/knxprod/README.md](../tools/knxprod/README.md)).
2. Add "H8i8o 8 inputs 8 outputs" to the project and set its individual address.
3. Program the individual address: press the programming button once when ETS asks.
4. Download the application. The device restarts at the end and runs with the new
   configuration.

A new device answers at 15.15.255 until it has an address. The individual address, the
group address and association tables and the parameters are kept in flash; they survive
restarts and bus power cuts.

ETS shows the manufacturer as "KNX Association": the product uses the example manufacturer
ID 0x00FA, as explained in the [README](../README.md#status-and-limitations).

### Factory reset

There is no factory reset by button. To return the device to its factory state (address
15.15.255, no tables, no parameters), erase the whole flash with the debug probe and program
the firmware again ([README](../README.md#flash-the-board)).
