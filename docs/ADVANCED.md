# Advanced guide — writing device models and environments

> 日本語: [ADVANCED.ja.md](ADVANCED.ja.md)
> Read the [getting started guide](GUIDE.md) first.

This guide is for writing your own models rather than using the ones that
exist. Its subject is `src/embedbench_device.h` — **the one surface in
this project that is fixed**.

## 1. Why only that one file is fixed

Environment implementations necessarily differ per platform (an Arduino
core, plain C++, some future runtime). Fixing something that has to
differ achieves nothing.

**Device models, on the other hand, should not differ.** A BME280 behaves
the same wherever it runs. So the fixed surface is that single header,
and it is plain C++11 with no platform header of any kind.

```text
        ┌──────────────────────────────────┐
        │   Environment (example, differs)  │
        └─────────────────┬────────────────┘
                          │
        ═════════ embedbench_device.h ═════════   ← only this is fixed
                          │
        ┌─────────────────┴────────────────┐
        │  Device models (portable, same)   │
        └──────────────────────────────────┘
```

The record of the freeze and the rules for changing it are in
[DEVICE_IF_FROZEN.ja.md](DEVICE_IF_FROZEN.ja.md) (Japanese). It is
currently version 1, revision 004.

**The rule:** when something does not work during verification, before
working around it in surrounding logic, ask first whether **changing the
interface is the simpler answer**. If it is, ask the maintainer to
approve the change. Frozen means "not changed without approval", not
"never changed".

## 2. The two classes

### `Device` — what you write

Override only the paths your part has; everything has a default except
`reset()`.

| Method | When it is called |
| --- | --- |
| `reset()` | Return to power-on state (**required**). |
| `i2cWrite` / `i2cRead` | An I2C transfer. The address belongs to the binding; the model sees only the payload. |
| `spiTransfer` | One SPI byte. Chip select arrives separately, as a line. |
| `serialIn` | Serial reception. A **byte stream**, not lines. |
| `lineIn` | A line the application drives (chip select, reset, trigger). |
| `frameIn` | Protocols with no dedicated port (see below). |
| `channelWrite` / `channelRead` | The **world**: the temperature changed, the button was pressed. |
| `advanceTo(nowUs)` | Time moved. Self-driven behaviour lives here. |
| `dump(out, cap)` | A human-readable summary of the state. |

`channel` is a path of its own for the outside world. It is not a bus and
not a pin: think of it as **how a test moves physical reality** (the room
went dark, the button was pressed, the target is now three metres away).

### `HostPort` — what the environment provides

How a model reaches outwards, available through `port()`.

| Method | Purpose |
| --- | --- |
| `nowMicros()` | The current virtual time (**required**). |
| `lineOut(line, level)` | Drive an output line (**required**). |
| `serialOut(data, len)` | Send serial (**required**). Any byte value survives. |
| `analogOut` / `analogOutMilliVolts` | Present an analog value (revision 002). |
| `requestWake(whenUs)` | "Advance me at this time" (revision 003). |
| `diagnose(text)` | Say what a return value cannot (revision 004). |
| `frameOut(bus, format, data, bits)` | Send a generic frame. |
| `formatId(name, schema)` | Register a format name and get its id. |
| `maxFrameBits(bus)` | What one frame on that link can carry. |

## 3. The contracts

This is the substance. Breaking one of these produces symptoms a long way
from the cause.

### 3.1 Effect-free methods

**`reset()`, `channelRead()` and `dump()` must not call `HostPort`.**

If an inspection rewrites the record, observing changes the thing
observed. And `reset()` can be called before the environment is ready.

A consequence: **`reset()` cannot capture a time base.** Neither can
`attach()`, which is not virtual and only stores the port. **Lazy capture
is the only way** (`unit_rtc_model` is the worked example).

```cpp
uint32_t UnitRtcModel::nowSeconds() const {
  if (!based_ || port() == nullptr) return baseSec_;
  return baseSec_ + (port()->nowMicros() - baseUs_) / 1000000;
}
// baseUs_ is taken the moment the application first sets the clock
```

### 3.2 `reset()` is a power cycle, not a new part

For volatile models the two are the same, which is why it never came up.
**For a non-volatile part, what was written survives** (X58).

```cpp
UnitFlashModel::UnitFlashModel() {          // a fresh part is erased
  for (size_t i = 0; i < kSize; ++i) memory_[i] = 0xFF;
}
void UnitFlashModel::reset() {              // a power cycle: the array stays
  phase_ = kIdle; writeEnabled_ = false; busy_ = false;
  // memory_ untouched
}
```

Blanking it should take an erase command, the same as on the bench. A
program **in flight is dropped** by `reset()` — that is the contract's
"drop every pending due time", and it matches an interrupted write.

### 3.3 Re-entrancy does not happen

**The environment never re-enters a device that is running.** Effects
raised while a device runs (interrupts, frame deliveries) are held and
delivered after it returns.

So calling `frameOut()` will not call this model's `frameIn()` from
inside it. You can raise effects with your state consistent.

**But deferral has a capacity too** (four in the example implementation).
Raising many effects in one call overruns it. X56 hit this: ten chunks
sent at once, six lost.

**The model was wrong, not the environment.** A link with a limit on what
fits in a frame has a cost per frame too. Ten frames in no time at all
was never a real thing.

```cpp
// wrong: everything in one call
while (sent < len) { port()->frameOut(...); sent += take; }

// right: send one, arrange the next
emitNext();
port()->requestWake(port()->nowMicros() + kFrameGapUs);
```

### 3.4 The time contract

`advanceTo(nowUs)` is **monotonic**. It may be called repeatedly with the
same time, and **it may jump** — deliver **every** behaviour that fell in
the jump.

```cpp
void UnitImuModel::advanceTo(uint64_t nowUs) {
  if (!sampling_) return;
  while (nowUs >= nextSampleUs_) {     // while, not if
    /* take one sample */
    nextSampleUs_ += kSampleUs;
  }
  port()->requestWake(nextSampleUs_);
}
```

Written with `if`, samples are silently lost when the application is away
for a while. Written with `while`, the FIFO overflows — which is the
**correct failure** to reproduce.

**Re-arm `requestWake` every time.** The environment holds a finite
number of them (eight in the example), and a slot is spent **per distinct
moment, not per device** — devices waiting for the same instant share one.

### 3.5 Buffers are borrowed

The pointers handed to `i2cWrite`, `serialIn`, `frameIn` and
`channelWrite` are **valid only for that call**. Copy into your own
members anything you need later.

`unit_flash_model` got this wrong once. While a program was pending, the
application polling the status (chip select going low again) wiped the
staging and the write was lost. It now latches the page buffer when the
program starts, the same as real hardware.

### 3.6 One model, one endpoint

Composite hardware is built by **combining models**. That said, **one
model on two buses is allowed** and genuinely needed —
`unit_codec_model` takes its configuration over I2C and its audio over
SPI. What the control bus was told changes what the data bus returns, so
they cannot be modelled as separate parts.

## 4. Protocols with no dedicated port

IR, LoRa, UWB, a proprietary radio — there is no port for these and there
will not be one.

**The policy:** recovering meaning from bit-banged samples afterwards is
not possible. So what is carried is only the pair of **pre-encoding
logical bits plus format information**. Try the physical layer on real
hardware.

```cpp
// register the name, get the id — the environment allocates it, so no clashes
codeFormat_ = port()->formatId("m5.ir.nec.1",
                               ebdev::schemaFingerprint("u8 addr,u8 cmd"));
// packed MSB-first, and the unused low bits of the last byte must be zero
const uint8_t frame[2] = {address_, command_};
port()->frameOut(kBus, codeFormat_, frame, 16);
```

**Format names** are `<vendor>.<protocol>.<version>`, at most 19
characters. `schemaFingerprint` exists to catch the same name used for a
different layout; the environment diagnoses the mismatch.

**`bits` may be zero.** A frame with no payload at all — NEC's repeat
code, say — is sent as an empty frame.

**Splitting belongs to the format.** The interface answers capacity per
bus and **refuses an oversized frame whole**; it never truncates.
Numbering the pieces, marking the last one, and handling a link too small
to carry the header are the format's decisions (`unit_chunk_model` is the
worked example).

## 5. If you are writing an environment

There are two example implementations. If you write a third, run it
through the **conformance kit** (`tests/conformance/`): one probe and one
scenario, checking that both implementations reach the same verdict.

### Traps the example implementations actually fell into

**Hold wakes in a table, not a single slot.** The first version kept only
the earliest and discarded the rest after returning `true` for them. The
contract is "may advance more often than asked, **never less**", so that
was a plain violation. It never showed up with only one device (X52).

**Combining lines is the adapter's job.** The interface tells each device
only about its own line. When several parts share one line, a
pass-through adapter lets **whichever part releases first pull the line
out from under the one still asserting** (X59).

```cpp
static void setLine(size_t which, uint8_t level) {
  asserted[which] = level != 0;
  const uint8_t want = (asserted[0] || asserted[1]) ? 1 : 0;  // combine
  if (want != current) { current = want; ebhost::pinInject(...); }
}
```

**Iterate devices in a fixed order.** The order of effects landing on the
same microsecond is exactly the order the adapter walks its devices in.
Walking a hash table or sorting by pointer **loses reproducibility**.

**Never truncate the record silently.** The example folds a repeating
cycle into one round plus a count and the time of the last copy before it
discards anything, and only when nothing repeats does it spend the last
slot on a truncation notice.

## 6. Debugging

**The trace is empty.** Check `stats().windows`. Zero means `runBegin`
was never called, and bus or pin traffic before it is not visible to the
environment at all (installing the hooks is what `runBegin` does).

**Answers are rounded to tick boundaries.** Your `HostPort` did not
override `requestWake` — the base implementation does nothing and returns
`false`. This has happened here; the symptom was 1,000 us spacing where
500 us was intended.

**`diag.deferred_full` appears.** Too many effects in one call. Reshape
the model along the time axis, as in §3.3.

**`dropped` is not zero.** The record has gaps: the events are too varied
for any repeating cycle to be folded. Consider whether a path that can
summarise at the moment of the call (a transaction-level record) applies.

**What is `x8..007000` in the trace?** A folded repeat: this line
happened eight times, and the last was at 007000.

## 7. Models worth reading

| To learn | Read |
| --- | --- |
| I2C basics, mandatory repeated start | `env_sensor_model` |
| Line-oriented serial | `gps_model` |
| Binary serial, framing by silence | `unit_modbus_model` |
| Where time itself is the measurement | `unit_sonic_model` |
| Self-driven sampling and a FIFO | `unit_imu_model` |
| A device with a time unit of its own | `unit_rtc_model` |
| A protocol locked by sequence | `unit_flash_model` |
| One part on two buses | `unit_codec_model` |
| The frame path and empty frames | `unit_ir_model` |
| Splitting for per-bus capacity | `unit_chunk_model` |
| Breaking on purpose | `unit_faulty_model` |

All twenty-two are listed in the [catalog](DEVICE_CATALOG.ja.md)
(Japanese). The reasoning and measurements behind every decision are in
the [experiment ledger](EXPERIMENTS.ja.md) (Japanese), X0 through X60.
