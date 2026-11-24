# Design: channel functions for the inputs and outputs

| | |
|---|---|
| Status | **Built**, 23 November 2026: steps 1-7 of §7, host-tested; not yet run on a board (§9). Figures marked "measured" are from the local build (Arm GNU Toolchain 13.2.Rel1, `MinSizeRel`, LTO). |
| Goal | Make the H8i8o a general-purpose binary input and switch actuator: each channel gets a function chosen in ETS, with the objects and parameters that function needs, in one firmware image per variant. |
| Builds on | KNstaX's parameter model (`parameter-model.md`), its transmit policies and rate limiter (`group_object.hpp`, `telegram_rate_limiter.hpp`), and the flash log's product records. |

## 1. What changes for the user

Today every input sends its contact state on DPT 1.001, and every output has a switch and a
status object. After this work:

- **An input** can be a switch or sensor contact (with a choice of what to send on closing and
  opening; as switch, alarm, open/close, occupancy or window/door contact, each with its own
  datapoint type), a short/long push button, a dimming push button, a shutter push button, a
  value, percentage or scene transmitter, or a pulse or energy counter. Every input can be
  locked from the bus, sent cyclically, and sent once after start.
- **An output** can be a plain switch (with on and off delays), a staircase light (with an
  optional warning before it goes off), a pulse output (door opener, gong), or a heating valve
  driven by pulse width from a 0–100 % control value. Every output can be locked, forced (2-bit
  priority object), switched by a central object, and recall or learn scenes.
- **The device** gets an "in operation" heartbeat, a central switch object, a delay before it
  sends anything after a start (so a whole installation does not talk at once), and a telegram
  rate limit.

ETS shows only the objects and parameters the chosen function uses, with the datapoint type
that function sends.

## 2. Constraints

### 2.1 Measured

| | Figure |
|---|---:|
| Flash image region | 122,880 B |
| Image today (application version 4, Data Secure) | 117,152 B: **5,728 B free** |
| Data Secure's share of the image (docs/data-secure.md) | about 22,600 B |
| RAM | 20,480 B: 5 KiB stack, 1,304 B static, 9,744 B heap after start, so about 4.3 KB free |
| Application code today (`startDevice`, `serviceTick`, `handleCommand`, `publishStatus`, `printCertificate` and smaller) | about 12 KB, of which `startDevice` alone is 7,968 B (the stack's start inlined into it) |

### 2.2 What KNstaX gives and does not give

- **Parameters are cheap**: one image in RAM, a default image and a bounds table in flash, code
  per read site (parameter-model.md §4.5). About 400 B of parameters cost about 400 B of RAM
  and roughly 1 KB of flash.
- **Objects cost** 16 B of flash and about 30 B of heap each, plus a binding entry (8 B) and a
  small thunk per bound handler.
- **Cyclic sending and the rate limit exist**: `setTransmitPolicy(port, {cyclicIntervalMs})` and
  `setTelegramRateLimit()`. The application does not need its own.
- **The exporter has no conditional objects**, no alternative datapoint types on one object
  number and no channels (P4 / K9, not built). Without the first two, ETS would list every
  object of every function on every channel with one fixed datapoint type.
- **A port's datapoint type is fixed at compile time** and the runtime encodes a value by its
  size class. A 1-byte port encodes 5.010, 5.001, 17.001 and 18.001 identically, a 1-bit port
  encodes every DPT 1, and a 4-octet port encodes 12.001 and 13.010 identically for values below
  2³¹. So one port per *size* serves every function that needs that size, and only ETS has to
  know which datapoint type the function means.

## 3. Architecture

### 3.1 Layers

```
 app/product.hpp        ports, parameters, object views, identity  (firmware + product file)
 app/logic/*.hpp,.cpp   input and output channel logic, debounce    (no KNstaX, no hardware:
                                                                      host-tested)
 app/main.cpp           hardware glue: pins, SAVE, flash records, bindings, publish, RTT
```

- **The logic** is plain C++ with one object per channel: `InputChannel` and `OutputChannel`.
  It is driven by a 1 ms tick and by events (a debounced contact edge, an object written from the
  bus), and it reports what to send through a small `Sink` interface and what to drive through
  `OutputChannel::on()`. It knows nothing about KNstaX types, GPIO or flash, so the same code
  runs in host tests on recorded timelines.
- **The glue** turns the parameter view into the logic's plain `InputConfig` / `OutputConfig`
  structs, binds each object to the logic, turns `Sink` calls into `publish()`, and applies
  output changes to the pins (operating mode, bus power failure, status telegrams) as today.
- **Reconfiguring keeps state.** `configure()` replaces a channel's settings and leaves its
  run-time state alone, because a parameter change from the device itself (scene learning,
  `setParameter()`) runs the same `onParametersChanged` path as an ETS download.

### 3.2 Objects: slots per size, views per function

Each channel has a fixed set of object **slots**, one per size class it can need. A function
uses some of them; ETS shows only those, each with the text and datapoint type of the function.

| Input slot | Size | Flags | Used by |
|---|---|---|---|
| A | 1 bit | C R W T | Switch/sensor (1.001, 1.005, 1.009, 1.018, 1.019), short press (1.001), dimming switch (1.001), shutter move (1.008) |
| B | 1 bit | C T | Long press (1.001), shutter stop/step (1.007) |
| C | 4 bit | C T | Dimming (3.007) |
| D | 1 byte | C T | Value (5.010, 5.001, 17.001), scene (18.001) |
| E | 4 bytes | C R W T | Counter (12.001 pulses, 13.010 energy in Wh) |
| L | 1 bit | C W | Lock, any function |

| Output slot | Size | Flags | Used by |
|---|---|---|---|
| S | 1 bit | C W | Switch, staircase, pulse (1.001) |
| T | 1 bit | C R T | Status, any function (1.001) |
| K | 1 bit | C W | Lock (1.001: 1 = locked) |
| F | 2 bit | C W | Forced operation (2.001) |
| V | 1 byte | C W | Scenes (18.001) for switch, staircase and pulse; control value (5.001) for the heating valve |

| Device object | Size | Flags | |
|---|---|---|---|
| Central switch | 1 bit | C W | Outputs whose "Central switch" is on follow it |
| In operation | 1 bit | C R T | Sends 1 every heartbeat period |

Object numbers follow the channel: input *n* (0-based) has its slots at `n × slots + slot`, the
outputs follow the inputs, the device objects come last. A function change never renumbers an
object. A slot that a build leaves out (§3.5) is not declared, so object numbers differ between
variants; variants are different application programs anyway.

**The binary contact keeps its meaning.** "Switch" with the default actions (closing = on,
opening = off) sends exactly what today's input sends.

### 3.3 Object views (KNstaX change)

A new export-only list, `kObjectViews`, says when ETS shows an object and how:

```cpp
inline constexpr auto kObjectViews = std::array{
    views::view<Port::In1A>({.instance = 0, .visible = params::when<In::Function>(InputFunction::Switch),
                             .function = "Switch", .dpt = {1, 1}}),
    views::view<Port::In1A>({.instance = 0, .visible = params::when<In::Function>(InputFunction::Dimming),
                             .function = "Switch (short press)", .dpt = {1, 1}}),
    ...
};
```

- A view names a port, the instance of the repeated parameter block its condition refers to, a
  condition in the existing `params::Condition` form, and optionally the object's function text
  and datapoint type.
- The exporter emits one `ComObject` per port and one `ComObjectRef` per view, each
  `ComObjectRefRef` inside `<choose ParamRefId=…><when test=…>` of the controlling parameter.
  Several views of one port are alternatives on one object number. A port without views is
  emitted as today, always visible.
- The exporter refuses a view whose datapoint type is not the port's size, and two views of one
  port that test the same parameter with overlapping values (ETS would show the object twice).
- The list is a separate `constexpr` object passed to the export only
  (`exportCommissionedProductToJson(kProduct, kObjectViews)`, `knx_commissioned_product(... VIEWS
  <symbol>)`). The firmware never references it, so it costs no flash.

This is the part of P4/K9 the H8i8o needs (conditional objects and alternatives). Channels as
ETS `<Channel>` elements and port repetition stay open; the ports are declared with a macro per
channel instead.

### 3.4 Parameters

Three blocks: a device block once, the input block eight times, the output block eight times.
Every function-specific parameter has a visibility condition on the channel's function, so a
channel page shows only what its function uses. Conditions are export data: firmware always
branches on the function itself, never on whether a parameter is visible.

The existing parameters keep their meaning. `Out::Channel` (Disabled / Enabled) becomes
`Out::Function` (Disabled / Switch / Staircase / Pulse / Heating valve) with Enabled = Switch = 1,
so its stored value means the same.

### 3.5 Build variants (Kconfig)

Not everything fits next to Data Secure, so the firmware is built in **variants**. A variant is
one defconfig file, `variants/<name>.defconfig`, holding both kinds of setting:

- `CONFIG_KNX_*` lines go to KNstaX's `knx_configure()` as an override fragment on top of
  `knx.defconfig` (Data Secure on or off, table sizes);
- `CONFIG_H8I8O_*` lines are resolved against the H8i8o `Kconfig` (the application's options:
  which functions, the product identity) with KNstaX's own generator,
  `tools/kconfig/knx_genconfig.py`, into `h8i8o_config.h`.

CMake splits the file by prefix, so a variant is one file, and both halves are checked by
Kconfig: an unknown or misspelt symbol fails the build. The firmware and the product file read
the same variant, so the product file always declares what that firmware implements.

| Symbol | Default | Effect |
|---|---|---|
| `H8I8O_INPUT_DIMMING` | y | Dimming push button; input slot C |
| `H8I8O_INPUT_SHUTTER` | y | Shutter push button (uses slots A and B) |
| `H8I8O_INPUT_VALUE` | y | Value and scene transmitter; input slot D |
| `H8I8O_INPUT_COUNTER` | y | Pulse counter; input slot E; a flash record for the counts |
| `H8I8O_OUTPUT_FORCED` | y | Forced operation; output slot F |
| `H8I8O_OUTPUT_SCENES` | y | Four scenes per output with learning; output slot V |
| `H8I8O_OUTPUT_VALVE` | y | Heating valve (PWM); output slot V |
| `H8I8O_BUS_TEST` | n | The RTT bus test generator (`b`, `x`); a bench tool |
| `H8I8O_APPLICATION_NUMBER`, `H8I8O_PRODUCT_KEY`, `H8I8O_PRODUCT_NAME`, `H8I8O_HARDWARE_SERIAL` | | The product identity of the variant |

Two variants ship:

| Variant | Data Secure | Functions | Application |
|---|---|---|---|
| `standard` | yes | all but the counters, output scenes and forced operation (§9) | number 1, "H8i8o 8 inputs 8 outputs" |
| `extended` | no | all | number 2, "H8i8o 8 inputs 8 outputs, extended" |

`H8I8O_INPUT_SHUTTER` and `H8I8O_OUTPUT_VALVE` only take the function out of ETS: their logic
is small and always compiled, so they save no flash (§9).

Each variant is its own application program and its own catalogue product (product key and
hardware serial differ), so ETS never mistakes one for the other. Moving a device from one
variant to the other means flashing the other image and replacing the device in the ETS
project.

### 3.6 Persistence

- The output switch values keep their record (`kOutputStateKey`).
- The counters get a record of their own (`kCounterKey`, 8 × 4 octets), written when SAVE warns
  of a bus power failure, before a planned restart, and every 15 minutes while a count has
  changed. A count is never written per pulse: at 15 minutes and 40 B per write the flash log
  compacts about once a day, far inside the flash's endurance.
- Learned scenes are parameters: `setParameter()` stores the image with the stack's next
  coalesced save.
- `PersistencePolicy::schemaVersion` goes from 1 to 2 (the ports change). After the update the
  device comes up without its stored commissioning; the product records (FDSK, start counter,
  output states) survive, because a discard keeps them.

## 4. Inputs in detail

Common to every function:

| Parameter | Values | Default |
|---|---|---|
| Function | Disabled, Switch, Alarm / Window or door / Presence / Open-close contact, Short/long press, Dimming, Shutter, Value, Percentage, Scene, Pulse counter, Energy meter | Switch |
| Contact type | Normally open, Normally closed | Normally open |
| Debounce time | 10, 20, 30, 50, 100, 200 ms | 30 ms |
| Long press after | 0.3–5.0 s in 0.1 s steps | 0.5 s (functions that tell short from long) |
| Lock object | Disabled, Enabled | Disabled |
| Send after start | No, Yes | No (Switch, Counter) |
| Cyclic sending | 0 (off) to 65,535 s | 0 (Switch, Counter) |

"Closing" below means the contact becomes active (after the contact type is applied); "opening"
means it becomes inactive.

- **Switch.** Action on closing and on opening, each Nothing / On / Off / Toggle (default On and
  Off), on object A (1.001). The contacts behave the same and show A as Alarm 1.005,
  Window/door 1.019, Occupancy 1.018 or Open/close 1.009. Toggle sends the inverse of A, and A
  follows bus writes (a status address linked to A keeps a toggle in step with the load). A
  push button that toggles a light is Switch with closing = Toggle and opening = Nothing.

  *Changed while building:* the first draft had an "object type" parameter for the datapoint
  type of A. An ETS condition tests one parameter, so a view of A could not depend on both the
  function and the object type; the types became functions of their own, and likewise
  Percentage (of Value) and Energy meter (of Counter). `behaviourOf()` maps them to the
  behaviour they share.
- **Short/long press.** Short action (Nothing / On / Off / Toggle) on A, sent on release before
  the long time. Long action on B, sent when the long time elapses while held. A release after
  a long press sends nothing.
- **Dimming.** Direction: One button (alternating), Brighter, Darker. Short press: toggle A
  (one button), On (brighter), Off (darker). Long press: C = start (3.007, step code 1, up or
  down); release: C = stop (step code 0). One button: dims up when A is off, otherwise opposite
  to the last dim.
- **Shutter.** Direction: One button (alternating), Up, Down. Long press: A = move (1.008, 0 up,
  1 down). Short press: B = stop/step (1.007) in the same direction. One button: each long press
  moves opposite to the last move, a short press stops in the direction of the last move.
- **Value, Percentage.** On closing, D = the value: 0–255 as it is (5.010), or 0–100 % scaled to
  0–255 (5.001).
- **Scene.** Scene number 1–64. Short press: D = recall (18.001, number − 1). Long press: D =
  store (bit 7 set).
- **Counter.** Counts closings. E = count ÷ pulses per unit (12.001 pulses for Pulse counter,
  13.010 Wh for Energy meter), sent when it has risen by "send after change of" units, and
  cyclically. A bus write to E sets the count (in units), so a meter reading can be loaded.
  Persistence as §3.6.

**Lock.** While L is 1 the input sends nothing; a long press in progress is dropped. A counter
keeps counting while locked (it measures, it does not command). At unlock nothing is sent.

**After start.** Inputs take their current level without sending (as today). After the device's
start delay (§6), "Send after start" sends A (Switch: the action of the current level, if it is
On or Off) or E (Counter).

## 5. Outputs in detail

Common to every function: operating mode, status object and state after a restart, as today.

| Function | Parameters | Behaviour |
|---|---|---|
| Disabled | | Released; telegrams ignored |
| Switch | On delay, off delay (0–65,535 s) | S switches after the delay; an opposite command cancels a pending one |
| Staircase | Time (1–65,535 s, default 120), retrigger (default yes), off with 0 (default yes), warning time (0–255 s, default 0) | S = 1 switches on for the time; with a warning time the output goes off for 1 s that long before the end, then on until the end |
| Pulse | Length (0.1–25.5 s, default 1 s) | S = 1 switches on for the length (retriggers); S = 0 is ignored |
| Heating valve | Cycle (1–60 min, default 10), monitoring time (0 = off, 1–255 min), safety value (0–100 %, default 30 %) | V (5.001) sets the duty cycle; below 2 % the valve stays shut, above 98 % open; without a value within the monitoring time the safety value applies |

Priorities, highest first:

1. **Forced operation** (F, 2.001): control 1 forces on or off; control 0 ends forcing and the
   output takes the last value commanded underneath.
2. **Lock** (K = 1): "at lock start" Unchanged / On / Off; "at lock end" Unchanged / On / Off /
   Last command. Commands while locked are remembered, not applied.
3. **Commands**: S, the central object (if "Central switch" is Yes) and scenes; the last one
   wins. For the valve, V.

**Scenes** (switch, staircase and pulse): four slots, each a scene number (0 = unused, 1–64) and
an action (Off / On). A recall (18.001) with a matching number applies the action as a command.
A store (bit 7) writes the output's current state into the action of every slot with that
number, with `setParameter()`.

**Status** is sent after every command (as today, also when nothing changes) and whenever the
output changes by itself (a delay, a staircase, a pulse, the valve, forced, lock).

**Bus power failure** works as today: every output is released at once, and the switch values
are stored.

## 6. Device

| Parameter | Values | Default | |
|---|---|---|---|
| Delay after start | 0–255 s | 0 | Nothing is sent, and inputs and timers are not evaluated for sending, until it has passed |
| Heartbeat period | 0 (off) – 65,535 s | 0 | "In operation" sends 1 that often (stack cyclic policy) |
| Telegram limit | 0 (off), 1–50 per second | 0 | `setTelegramRateLimit()` for this device's own group sends |

## 7. Implementation plan

| Step | Work | Verified by |
|---|---|---|
| 1 | KNstaX: object views (C++ model, JSON, exporter, CMake `VIEWS`) on a branch of the submodule | New exporter tests; the existing exporter and XSD tests; the H8i8o product file |
| 2 | H8i8o: Kconfig and variants (CMake split, generated header, identity from the variant); bus test generator behind `H8I8O_BUS_TEST`; Data Secure code behind `KNX_SECURE_ENABLED` | Both variants build; `standard` image identical in behaviour to today |
| 3 | Logic modules and host test project (`test/`) | Host tests |
| 4 | Product: slots, parameters, views; schema and application version | Product file of both variants; XSD validation |
| 5 | Glue: inputs | Host tests; firmware build and size |
| 6 | Glue: outputs and device | Host tests; firmware build and size |
| 7 | Measure both variants, choose what `standard` carries, update docs | §9 table; docs/application.md, README |

### 7.1 Not in this round

- **Shutter actuator outputs** (two outputs as up/down with interlock, running time and
  position). Designed in outline: the odd output of a pair is the master, the pair is
  interlocked in the glue (never both on, 500 ms pause before reversing), S carries move (1.008)
  and K stop/step (1.007). It needs a hardware check of the output stage and a second 1-bit
  input slot per output, and it is safety relevant, so it waits for a bench test.
- **Hardware dimming (PWM)** of DO1–DO4, DO7, DO8: depends on the unknown output stage.
- **Analogue AUX input**: depends on how AUX is wired on the I/O board.

## 8. Testing

- **Host tests** (`test/`, MinGW or Linux GCC): the logic on timelines: input edges and ticks
  in, telegrams out; output commands and ticks in, pin state and status out. One test file per
  function family, each function's parameters at their defaults and at their edges (zero
  delays, retrigger off, lock during a delay, wrap of the millisecond counter).
- **Exporter tests** (KNstaX `tools/knxprod_exporter`): views become alternative
  `ComObjectRef`s under the right `<choose>`; a port without views is unchanged; overlapping
  views and wrong sizes are refused. The XSD test validates a generated file.
- **Builds**: both variants' firmware and product file; image and static RAM per variant.
- **On the board** (not possible in this session): ETS6 import, a download of each function,
  the heap after start (`s`), and one telegram trace per function. A checklist goes into
  docs/application.md.

## 9. Result

### 9.1 What was built

| Step | Result |
|---|---|
| 1 KNstaX object views | `knx/product/object_views.hpp`, `objectViewsToJson()`, `exportCommissionedProductToJson(product, views)`, `knx_commissioned_product(... VIEWS ...)`, the exporter's alternative `ComObjectRef`s and its checks; 12 exporter tests (two validate against the KNX schema) and a C++ test of the JSON. The schema check found an older exporter bug, fixed on the way: 2- and 4-bit objects were written as `2 Bits` / `4 Bits`, which the schema does not allow (`2 Bit`, `4 Bit`); no product had such an object before. Branch `object-views` of the KNstaX submodule. |
| 2 Variants | `Kconfig`, `variants/standard.defconfig`, `variants/extended.defconfig`, `cmake/h8i8o_variant.cmake`; Data Secure code behind `KNX_SECURE_ENABLED`, the bus test generator behind `CONFIG_H8I8O_BUS_TEST` |
| 3 Logic | `app/logic`: `InputChannel`, `OutputChannel`, debounce; `test/`: 62 host tests (30 input, 28 output, 4 debounce) |
| 4 Product | Slots, 3 parameter blocks, object views; application version 5, schema version 2 |
| 5, 6 Glue | `app/main.cpp` rewritten around the logic |
| 7 Docs | docs/application.md, firmware.md, data-secure.md, README, tools/knxprod/README.md |

### 9.2 Size

Measured, standard variant (Data Secure), one function off at a time:

| Build | Image | Function's cost |
|---|---:|---:|
| Application version 4 | 117,152 B | |
| Version 5, no optional function | 120,076 B | |
| Version 5, every function | 123,264 B (384 B over) | |
| without dimming | 123,100 B | 164 B |
| without value and scene | 123,112 B | 152 B |
| without pulse counter | 122,184 B | 1,080 B |
| without forced operation | 122,952 B | 312 B |
| without output scenes | 122,436 B | 828 B |
| without shutter, without valve | 123,264 B | 0 (logic always compiled) |

Two savings were found on the way, in every variant: the object handlers capture the channel
instead of being one lambda per object (KNstaX makes one thunk per callable type; −2,832 B),
and the channel logic starts from zero so it sits in `.bss` rather than `.data` (−860 B).

Ubuntu's toolchain (the documented Docker build) made version 4 1,244 B larger than the Arm
toolchain. To leave room for that, `standard` leaves out the counters, output scenes and
forced operation:

| | `standard` | `extended` |
|---|---:|---:|
| Image | 121,032 B (1,848 B free) | 101,048 B |
| Objects / parameters / image | 74 / 283 / 332 B | 90 / 347 / 396 B |
| Static RAM besides the stack | 2,096 B | 2,040 B |
| Heap after start | about 11.5 KB (estimated) | about 10 KB (estimated) |

### 9.3 Not verified

- **Nothing of version 5 has run on a board.** The ETS6 import of a file with conditional
  objects, a download, each function on the bus, and the heap after start (`s`) are open;
  docs/application.md has the check list.
- **The Docker build** could not be run here; its image size for `standard` is estimated.
- **The heap** is estimated (§9.2). With `H8I8O_TRACE` the `standard` variant may not have
  room for it.

### 9.4 Next

- If the board shows room: scenes into `standard` (they are the most asked for of the three).
- Cheaper scenes: learned actions as a product record instead of four `setParameter`
  instantiations.
- KNstaX: a generic handler per value type would remove the per-object binding code that
  remains, and channels (P4) would let ETS group each channel's objects.
- The shutter actuator outputs of §7.1, after a bench test of the output stage.
