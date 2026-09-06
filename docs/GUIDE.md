# Getting started

> 日本語: [GUIDE.ja.md](GUIDE.ja.md)

EmbedBench verifies **the logic of an embedded application on a host
machine, with no hardware attached**.

This guide is the first thing to read. When you get to writing your own
device models, continue with the [advanced guide](ADVANCED.md).

## 1. What it is for

Verifying against real hardware has weaknesses that are hard to design
away.

- **It does not reproduce.** "It fails sometimes" cannot be investigated.
- **Failures cannot be provoked.** Breaking a sensor on purpose is hard.
- **It is slow.** Waiting out a 7.5 ms measurement a hundred times costs
  real minutes.
- **It leaves no evidence.** Afterwards there is nothing to read back.

EmbedBench takes on all four. Time is virtual, so `delay(2100)` finishes
instantly; devices are models, so they can be broken on cue; and what
happened is kept as one line per event. The same input always produces
**the same record**.

### What it does not do

**The physical layer is out of scope.** Waveforms, timing margins,
electrical characteristics, noise immunity — those can only be confirmed
on real hardware, so they were excluded from the start.

What is in scope is **what the application was trying to do**. "Write
0xF4 to address 0x76, wait 7.5 ms, read three bytes" is the subject; how
many volts and nanoseconds that took is not.

## 2. The three participants

```text
  ┌──────────────┐        ┌────────────────┐        ┌───────────────┐
  │ Application  │ ─────> │  Environment   │ <────> │ Device model  │
  │ (unmodified  │ <───── │ (recording,    │        │ (thermometer, │
  │  Arduino)    │        │  time, buses)  │        │  GPS, a part  │
  └──────────────┘        └────────────────┘        │  that breaks) │
                                  │                 └───────────────┘
                                  v
                        one line per event
```

The **application** is the code you want to verify. It is **not
rewritten**. `Wire.beginTransmission()`, `digitalRead()` and `delay()`
all work exactly as written for the real board.

A **device model** is the behaviour of a part that is connected. There
are twenty-two worked examples in `tests/common_models/` (see the
[catalog](DEVICE_CATALOG.ja.md), Japanese).

The **environment** sits between them and records everything. There are
two example implementations — one for the host Arduino core
(`src/embedbench_host.*`) and one in plain C++ (`tests/common_env/nenv.*`).

> **Important:** the only thing that is fixed is **the boundary between
> device models and the environment** (`src/embedbench_device.h`). The
> environment itself is an example; its names and shape can still move.

## 3. Reading your first trace

Run one first.

```sh
cd tests
uv sync
uv run pytest units_gpio -q -s
```

`tests/units_gpio/` is a sketch with a button, a motion sensor, a relay
and an ultrasonic rangefinder attached. The heart of its output is this.

```text
01 000000 main dir chan.write chan=0 data=01
02 000000 main dev gpio.inject pin=26 1->0 match=1
03 000000 isr  core isr.enter pin=26
04 000000 isr  app  gpio.write pin=2 val=1
05 000000 isr  core isr.exit pin=26
```

Reading a line from the left:

| Column | Meaning |
| --- | --- |
| `01` | Sequence number. A gap means something was not recorded. |
| `000000` | Virtual time in microseconds. |
| `main` | The context: `main`, `tick` (time passing) or `isr`. |
| `dir` | Who caused it: `app`, `dev`, `dir` (the test), `core`, `diag`. |
| rest | What happened. |

Those five lines read as: the test pressed the button, the device pulled
its line low, the interrupt ran, the application lit an LED, the
interrupt returned. **Cause and effect survive, with times and order** —
that is the central thing EmbedBench gives you.

## 4. Writing your own test

One experiment is one directory.

```text
tests/myexperiment/
  myexperiment.ino      ← the sketch (code under test plus wiring)
  sketch.yaml           ← build profile (copy an existing one)
  test_myexperiment.py  ← the expectations
```

### The shape of a sketch

```cpp
#include <Arduino.h>
#include <EmbedBench.h>
#include <Wire.h>
#include <temp_model.h>          // a model from the catalog

static TempSensorModel sensor;

// 1) how the model reaches the outside world. DevicePort routes
//    everything to the environment already; all it needs is which board
//    pin each of the device's own lines corresponds to.
static ebhost::DevicePort port;

// 2) the world's channel reaches the model
static bool onChannel(uint8_t ch, const uint8_t* d, size_t n, void*) {
  return ch == 0 && sensor.channelWrite(TempSensorModel::kChannelTemp, d, n);
}

// 3) wiring the bus to the model
static uint8_t onWrite(const uint8_t* d, size_t n, bool stop, bool cont, void*) {
  const ebdev::I2cTransfer xfer = {stop, cont};
  return sensor.i2cWrite(d, n, xfer);
}
static size_t onRead(uint8_t* d, size_t n, bool stop, bool cont, void*) {
  const ebdev::I2cTransfer xfer = {stop, cont};
  return sensor.i2cRead(d, n, xfer);
}

void setup() {
  Serial.begin(115200);
  Serial.println("TEST start myexperiment");
  Wire.begin(21, 22, 400000);

  port.mapLine(TempSensorModel::kLineDataReady, 27);  // its line is pin 27
  sensor.attach(&port);
  const ebhost::WireDeviceOps ops = {&onWrite, &onRead, nullptr};
  ebhost::bindWireDevice(0x48, ops);  // this model answers at this address
  ebhost::setChannelHandler(&onChannel);
  sensor.reset();

  ebhost::runBegin(1000);                // start recording, 1,000 us tick

  // The world puts a temperature on the sensor. This is what `channel`
  // is for: not a bus and not a pin, but the test moving reality.
  const uint8_t reading[2] = {0x00, 0xFA};
  ebhost::chanWrite(ebhost::Origin::kDir, 0, reading, 2);

  // --- everything below is the code under test, unchanged ---
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(uint16_t(0x48), size_t(2), true);
  const int hi = Wire.read();
  const int lo = Wire.read();
  // ----------------------------------------------------------

  ebhost::runEnd();                      // stop recording

  static char trace[2048];
  ebhost::formatTrace(trace, sizeof(trace));
  Serial.printf("values temp=%02X%02X\n", hi, lo);
  Serial.print(trace);
  const ebhost::Stats s = ebhost::stats();
  Serial.printf("stats events=%u dropped=%u diag=%u\n",
                s.events, s.dropped, s.diagCount);
  Serial.println("TEST done");
}

void loop() { delay(10); }
```

### Writing the expectations

```python
def test_myexperiment(dut):
    dut.expect("TEST start myexperiment", timeout=10)
    dut.expect("values temp=00FA", timeout=10)
    dut.expect("01 000000 main dir chan.write chan=0 data=00FA", timeout=10)
    dut.expect("02 000000 main dev gpio.inject pin=27 0->1 match=0", timeout=10)
    dut.expect("06 000000 main dev i2c.rd.resp len=2 data=00FA re=5", timeout=10)
    dut.expect("stats events=6 dropped=0 diag=0", timeout=10)
    dut.expect("TEST done", timeout=10)
```

Pinning exact lines looks tedious, but it is what makes **any change to
the record impossible to miss**. If the change was intended, update the
expectation; if it was not, you have just caught a regression.

## 5. What to assert on

This is the most important habit here.

> **Assert on values, not on the absence of errors.**

`tests/units_misuse/` demonstrates why. Do an I2C read without selecting
a register first: the bus is fine, the part answers, the status is zero —
and what comes back is **some other register's value**.

```text
08 000000 main dev i2c.rd.resp len=2 data=0100 re=7
```

No status code can report that the sketch asked the wrong question. An
assertion on the value catches it; watching only for errors does not.

### Three numbers worth reading every time

```text
stats events=15 dropped=0 diag=6 outside=2 windows=1
```

| Number | What it tells you |
| --- | --- |
| `dropped` | **Not zero means the record has gaps**, so expectations may not mean what they look like. |
| `diag` | How many diagnostics the environment added. Check any you did not expect. |
| `windows` | **Zero means you forgot `runBegin`**, and the empty trace is explained. |

## 6. Common stumbles

**The trace is empty and `windows=0`.**
`ebhost::runBegin()` was never called. Installing the host hooks is what
`runBegin` does, so bus traffic before it is not merely unrecorded — the
environment is **not connected to anything yet**.

**`delay(500)` finishes instantly.**
That is the point: time is virtual, so waiting costs no real time. Note
that `delay()` is milliseconds and `delayMicroseconds()` is microseconds
— if your event times are a thousand times larger than expected, that is
the reason.

**`diag.unbound addr=XX` appears.**
No model is bound to that address. Check your `ebhost::bindWireDevice()`.

**A device answers later than it should.**
Either the model never calls `requestWake()`, or your `HostPort` did not
override it. Without it, answers are rounded up to the next tick.

**A `dut.expect` never matches.**
`*`, `.` and `+` are regex characters. Wrap the pattern in `re.escape()`.

## 7. Where to go next

- [Advanced guide](ADVANCED.md): writing your own device models.
- [Device catalog](DEVICE_CATALOG.ja.md) (Japanese): the twenty-four
  models and how to pick one.
- [tests/README.md](../tests/README.md): an index of the 62 experiments.
- [Experiment ledger](EXPERIMENTS.ja.md) (Japanese): why the design is
  what it is, with measurements.
