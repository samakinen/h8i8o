# The ETS application

What the device does on the KNX bus, and how ETS configures it. Application version 5, in two
build variants ([firmware.md](firmware.md#build-variants)):

| Variant | Application | KNX Data Secure | Functions |
|---|---|---|---|
| `standard` | `M-00FA_A-0001-05`, "H8i8o 8 inputs 8 outputs" | yes | all but the counters, output scenes and forced operation, marked *extended* below |
| `extended` | `M-00FA_A-0002-05`, "H8i8o 8 inputs 8 outputs, extended" | no | all |

Each input and each output has a **function**, chosen on its parameter page. ETS shows only
the parameters and objects that function uses, each object with the datapoint type the
function sends. [design/channel-functions.md](design/channel-functions.md) is the design
behind it.

## Communication objects

Every channel has a fixed set of objects, so changing a function never renumbers one. Which of
them ETS shows, and as what, depends on the function (tables below).

| Objects | Per | |
|---|---|---|
| A, B, C, D, E, Lock | input 1–8 | C needs dimming, D the value and scene functions, E the counters; a variant without them has no such objects, and the numbers close up |
| Switch, Status, Lock, Forced, V | output 1–8 | Forced needs forced operation, V scenes or the valve |
| Central switch, In operation | device | the last two objects |

| Objects | `standard` | `extended` |
|---|---|---|
| Input *n* (1–8) | 5(*n*−1) … +4: A, B, C, D, Lock | 6(*n*−1) … +5: A, B, C, D, E, Lock |
| Output *n* (1–8) | 40 + 4(*n*−1) … +3: Switch, Status, Lock, V | 48 + 5(*n*−1) … +4: Switch, Status, Lock, Forced, V |
| Central switch, In operation | 72, 73 | 88, 89 |

The device holds up to 64 group addresses and 96 associations.

## Inputs DI1–DI8

### Functions

| Function | Object A | Object B | Other | Behaviour |
|---|---|---|---|---|
| Disabled | | | | Sends nothing |
| Switch | 1.001 Switch | | | An action on closing and one on opening, each Nothing / On / Off / Toggle (default On and Off) |
| Alarm contact | 1.005 Alarm | | | As Switch |
| Window or door contact | 1.019 Window/door | | | As Switch |
| Presence contact | 1.018 Occupancy | | | As Switch |
| Open/close contact | 1.009 Open/close | | | As Switch |
| Short and long press | 1.001 short press | 1.001 long press | | Short press: an action on A on release. Long press: an action on B when the long time is reached |
| Dimming | 1.001 Switch | | C: 3.007 Dimming | Short press switches, long press dims, release stops |
| Shutter | 1.008 Move | 1.007 Stop/step | | Long press moves, short press stops or steps |
| Value (5.010) | | | D: 5.010 | Sends the value 0–255 on closing |
| Percentage (5.001) | | | D: 5.001 | Sends the value 0–100 % on closing |
| Scene | | | D: 18.001 Scene control | Short press recalls the scene, long press stores it |
| Pulse counter (12.001) *extended* | | | E: 12.001 | Counts closings |
| Energy meter, Wh (13.010) *extended* | | | E: 13.010 | Counts closings, as energy |

Dimming, shutter, value, percentage, scene and the counters are functions a build variant
can leave out (Kconfig); `standard` leaves out the counters, for lack of flash next to Data
Secure.

"Closing" means the contact becomes active, "opening" that it becomes inactive, after the
contact type is applied.

### Parameters

| Parameter | Values | Default | Shown for |
|---|---|---|---|
| Function | see above | Switch | always |
| Contact type | Normally open / Normally closed | Normally open | every function |
| Debounce time | 10, 20, 30, 50, 100, 200 ms | 30 ms | every function |
| On closing, On opening | Nothing / On / Off / Toggle | On, Off | Switch and the contacts |
| Short press, Long press | Nothing / On / Off / Toggle | Toggle, Toggle | Short and long press |
| Long press after (× 0.1 s) | 3–50 | 5 (0.5 s) | Short and long press, Dimming, Shutter, Scene |
| Button | One button (alternating) / Brighter / Darker | One button | Dimming |
| Button | One button (alternating) / Up / Down | One button | Shutter |
| Value | 0–255 (Percentage: 0–100) | 0 | Value, Percentage |
| Scene number | 1–64 | 1 | Scene |
| Pulses per unit | 1–65,535 | 1 | the counters (*extended*) |
| Send after an increase of (units) | 1–65,535 | 1 | the counters (*extended*) |
| Lock object | Disabled / Enabled | Disabled | every function |
| Send after a start | No / Yes | No | Switch, the contacts, the counters |
| Send cyclically every (0 = never) | 0–65,535 s | 0 | Switch, the contacts, the counters |

### Behaviour

- **Switch and the contacts.** Toggle sends the opposite of object A's value. A is also
  written by the bus, so a toggle continues from the state another device reported: link A to
  the actuator's status address as well. A push button that toggles a light is Switch with
  "On closing" = Toggle and "On opening" = Nothing. The defaults send 1 while the contact is
  closed and 0 while it is open, as the input did before application version 5.
- **Short and long press.** A short press sends on release, before the long time; a long
  press sends as soon as the time is reached, and its release sends nothing.
- **Dimming.** Short press: one button toggles A; "Brighter" sends on, "Darker" off. Long
  press: C = dim up or down by 100 % (step code 1); release: C = stop. One button dims up while
  A is off, otherwise the opposite way from last time (a lit light dims down first).
- **Shutter.** Long press: A = move (0 up, 1 down). Short press: B = stop/step (0 up, 1 down).
  One button moves the opposite way from its last move and stops in the direction of the
  last move.
- **Value, Percentage.** Sent when the contact closes. A percentage above 100 is sent as
  100 %.
- **Scene.** Short press: D = recall (scene number − 1). Long press: D = store (bit 7 set).
- **Counters** (*extended*). Every closing is a pulse. E is the count divided by "pulses per unit"
  (a meter with 1,000 pulses per kWh and the energy meter function: 1 pulse per unit gives
  Wh). E is sent each time it has risen by the set amount, and cyclically if set. Writing E
  sets the count, in units, so a meter reading can be loaded. The counts are kept in flash
  (when the bus power fails, before a planned restart and every 15 minutes while they change),
  so a restart loses at most the last 15 minutes' pulses after a reset without warning. A
  factory reset clears them. Counting is done every millisecond after debouncing: pulses of
  at least the debounce time, with at least as long a gap, are counted.
- **Lock.** With the lock object enabled, writing 1 locks the input: it sends nothing, and a
  press in progress is dropped. A counter keeps counting while locked and sends at the next
  increase after the lock ends. Writing 0 unlocks; nothing is sent then.
- **After a start** the inputs take their current state without sending. Once the device
  sends (after its start delay, see Device), "Send after a start" sends A (the action of the
  current contact state, if it is On or Off) or E.
- **Cyclic sending** repeats the last value of A (or E).
- Nothing is sent while programming mode is on, before the device has an individual address,
  or before the start delay is over; the state is tracked meanwhile.

An input is active when its pin is pulled to ground; see [hardware.md](hardware.md).

## Outputs DO1–DO8

### Functions

| Function | Switch object | V object | Behaviour |
|---|---|---|---|
| Disabled | | | Released, whatever the objects say |
| Switch | 1.001 Switch | 18.001 Scene *extended* | On and off, each with an optional delay |
| Staircase light | 1.001 Staircase | 18.001 Scene *extended* | On for a time, with an optional warning before it goes off |
| Pulse | 1.001 Pulse | 18.001 Scene *extended* | On for a short time per on telegram (door opener, gong) |
| Heating valve (PWM) | | 5.001 Control value | Switched by pulse width from the control value |

The status object (1.001) reports the output as the switch object means it (operating mode
applied). The lock object (1.001, 1 = locked) and, in `extended`, forced operation (2.001)
exist for every function except Disabled. Forced operation, scenes and the heating valve are
options a build variant can leave out (Kconfig); `standard` leaves out scenes and forced
operation, for lack of flash next to Data Secure.

### Parameters

| Parameter | Values | Default | Shown for |
|---|---|---|---|
| Function | see above | Switch | always |
| Operating mode | Normally open (1 = energise) / Normally closed (1 = de-energise) | Normally open | every function |
| Status object | Disabled / Send on change | Send on change | every function |
| State after a restart | Released / As before the restart or bus power failure | Released | Switch, Staircase, Pulse |
| Switch-on delay, Switch-off delay | 0–65,535 s | 0 | Switch |
| Staircase time | 1–65,535 s | 120 s | Staircase |
| Retrigger while on | No / Yes | Yes | Staircase |
| Switch off with 0 | No / Yes | Yes | Staircase |
| Warn this long before the end (0 = no warning) | 0–255 s | 0 | Staircase |
| Pulse length (× 0.1 s) | 1–255 | 10 (1 s) | Pulse |
| Lock object | Disabled / Enabled | Disabled | every function |
| When the lock starts | Unchanged / On / Off | Unchanged | lock enabled |
| When the lock ends | Unchanged / On / Off / As commanded meanwhile | Unchanged | lock enabled |
| Follows the central switch | No / Yes | No | Switch, Staircase, Pulse |
| Scene A–D number (0 = unused), Scene A–D (*extended*) | 0–64; Off / On | 0; Off | Switch, Staircase, Pulse, on a page of their own |
| Cycle time | 1–60 min | 10 min | Heating valve |
| Safety value after no control value for (0 = never) | 0–255 min | 0 | Heating valve |
| Safety value | 0–100 % | 30 % | Heating valve |

### Behaviour

- **Priorities**, highest first: forced operation, then the lock, then the commands (the
  switch object, the central switch, scenes; for the valve, the control value). What is
  commanded underneath a lock or forced operation keeps running (a staircase timer, say) and
  takes over when they end, as the lock-end setting says.
- **Switch.** With a delay the output switches when it has run out. A command the other way
  cancels the pending one; a repeat of the pending command keeps its deadline.
- **Staircase.** 1 switches on for the staircase time; with "retrigger" a further 1 restarts
  it. 0 switches off if "switch off with 0" is set. With a warning time the output goes off for
  one second that long before the end, then on again until the end.
- **Pulse.** 1 switches on for the pulse length (and restarts it); 0 does nothing.
- **Heating valve.** The control value sets the share of each cycle the valve is open. Below
  about 2 % it stays shut, above about 98 % it stays open. Until the first control value, and
  after none has arrived within the monitoring time, the safety value applies (without
  monitoring, the valve stays shut until the first value). The operating mode serves
  normally closed and normally open valve drives.
- **Lock.** 1 locks: the output goes to the "lock starts" state. Commands meanwhile are
  remembered, not applied. 0 unlocks: "Unchanged" keeps the output as the lock left it, On or
  Off switch it, "As commanded meanwhile" applies what was commanded during the lock.
- **Forced operation** (2.001, *extended*): control 1 forces the value (on or off); control 0 ends it, and
  the output takes the state underneath.
- **Central switch.** Outputs set to follow it treat it as their switch object.
- **Scenes** (*extended*). A recall (18.001) of a scene number set in one of the four slots applies that
  slot's action as a command. A store (bit 7 set) writes the output's current state into the
  action of every slot with that number; the device keeps it as a parameter, so it survives a
  restart, and ETS overwrites it with its own value at the next download.
- **Status** is sent after every command, also one that changes nothing, and whenever the
  output changes by itself (a delay, the staircase, a pulse, the valve, forced operation,
  the lock). With "Disabled" it is never sent but still answers reads.
- **When bus power fails**, every output is released at once, as soon as the transceiver
  warns of it, and the outputs' switch values are stored. The board runs from the bus, so the
  outputs would drop anyway when the supply gives out; releasing them first leaves the
  device time to store its state. If the bus comes back before the supply gives out, the
  outputs follow their functions again.
- **State after a restart** (after an ETS download, a bus power cut or any other restart):
  - "Released": the output is released (pin low), and no status is sent. A Switch output in
    "Normally closed" mode therefore reads as on.
  - "As before the restart or bus power failure": the output gets its stored switch value as a
    command (so a staircase starts its time again), and its status is sent once the device
    sends. The value is stored when the bus power fails and before a planned restart; after an
    unplanned reset (the watchdog, say) the output takes the last stored value.

## Device

| Parameter | Values | Default | |
|---|---|---|---|
| Delay before sending after a start | 0–255 s | 0 | Nothing is sent before it is over. Give the devices of a line different delays, so they do not all send at once after a power cut |
| "In operation" every (0 = never) | 0–65,535 s | 0 | Object In operation (1.002) sends 1 that often; ETS shows the object only when this is set |
| Telegrams per second at most (0 = no limit) | 0–50 | 0 | A limit on the device's own group telegrams |

The **central switch** object (1.001) switches every output set to follow it.

## Programming button and LED

- A press of the programming button toggles programming mode. Press it **once** when ETS
  asks: a second press ends programming mode before ETS has written the address.
- The LED is on while programming mode is on, and off otherwise.
- Programming mode ends when the button is pressed again, when the device restarts, or by
  itself after 15 minutes.
- **Factory reset**: hold the button for 10 seconds, until the LED flashes fast, and release
  it while it flashes (within 5 seconds). Held longer, nothing happens.

## Commissioning with ETS6

1. Build the firmware and the product file of the same variant; sign and import the product
   file ([ets-product.md](ets-product.md)).
2. Add the product ("H8i8o 8 inputs 8 outputs", or the extended one) to the project and set
   its individual address.
3. Program the individual address: press the programming button once when ETS asks.
4. Download the application. The device restarts at the end and runs with the new
   configuration.

A new device answers at 15.15.255 until it has an address. The individual address, the
group address and association tables and the parameters are kept in flash; they survive
restarts and bus power cuts.

ETS shows the manufacturer as "KNX Association": the product uses the example manufacturer
ID 0x00FA, as explained in the [README](../README.md#status-and-limitations).

The `standard` variant supports KNX Data Secure: any object can be secured in ETS, and none
has to be. Without security it is commissioned as above. With it, ETS also needs the device
certificate: [data-secure.md](data-secure.md#commissioning-with-ets6). The `extended` variant
is a plain device.

### Checking a download on the board

The functions are host-tested, but each needs checking once on a board with ETS6. For each
function: download it, operate the input (or send to the output), and compare the RTT log
(`DI<n> active`, `DO <bits>`) and the telegrams in the ETS group monitor with the tables
above. After the first download of version 5, `s` shows the heap after start; it must stay below
the room [firmware.md](firmware.md#memory) gives for it.

### Factory reset

Hold the programming button for 10 seconds and release it while the LED flashes fast. The
device returns to its factory state and restarts: address 15.15.255, no group addresses or
associations, default parameters, no stored output states or counts. It is the reset ETS does
with a Master Reset (erase code 02h), so it keeps what that keeps, such as the KNX Data
Secure sequence number. The device is back on its Factory Tool Key with Data Secure off, and
its device certificate is unchanged.

Erasing the whole flash with the debug probe and programming the firmware again also works
([README](../README.md#flash-the-board)), but it is not the same for KNX Data Secure: the
device loses its sequence number and makes a new Factory Tool Key, so it has a new device
certificate, and ETS must be given the new one.

### Updating from application version 4

Version 5 adds the functions, the objects and the device parameters. The objects are
renumbered (each channel's objects are now together), and the stored commissioning has a new
layout, so the device starts **uncommissioned** after the firmware update: address 15.15.255,
no tables. Its device certificate (Factory Tool Key) and its stored output states survive.

Import the new product file, change the device's application program in the ETS project to
version 5 (the `standard` variant keeps application number 1), check the links (inputs and
outputs keep their meaning with the default functions, but the object numbers change), program
the individual address with the programming button and download the application.

### Updating from application version 3

Version 4 adds KNX Data Secure; the objects and parameters are the same as in version 3.
Import the new product file, change the device's application program in the ETS project to
version 4 and download it. The device keeps its individual address, tables and parameters
across the firmware update.

### Updating from application version 2

Version 3 adds the "State after a restart" parameter, halves the group address and
association capacity (64 and 96), and uses extended frames (APDU length 55). Import the new
product file, change the device's application program in the ETS project to version 3,
and download it. Until then the device keeps its individual address and tables but starts
with default parameters, because the stored ones are of the old layout. A device linked to
more than 64 group addresses, or with more than 96 associations, does not fit version 3.
