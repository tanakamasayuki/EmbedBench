# Current state, in numbers

> 日本語: [FACTS.ja.md](FACTS.ja.md)

Figures other documents can cite. Every number here is checked by
`tests/facts/`, so a stale one fails a test run rather than travelling
into someone else's document.

Cite this page rather than counting for yourself: this project's state
moves, and the last three external quotations of it were each wrong in at
least one number.

<!-- FACTS BEGIN -->
| What | Value |
| --- | ---: |
| Device interface version | 1 |
| Device interface revision | 4 |
| Device interface, effective LOC | 140 |
| Device models in `devices/` | 23 |
| Device model source lines (.h + .cpp) | 3001 |
| Environment implementations | 2 |
| Host environment, effective LOC | 1465 |
| Native environment (`nenv`), effective LOC | 464 |
| Experiment directories | 64 |
| SD card preset volumes | 7 |
<!-- FACTS END -->

Effective LOC excludes blank lines and comment-only lines, the same
measure the ledger uses.

## What is fixed and what is not

**Fixed:** `src/embedbench_device.h`, the boundary between device models
and any environment. Pure C++11, no platform header, `<stddef.h>` and
`<stdint.h>` only. Changing it needs the maintainer's approval; the
record and the rules are in [DEVICE_IF_FROZEN.ja.md](DEVICE_IF_FROZEN.ja.md).

**Not fixed:** everything else. The host environment
(`src/embedbench_host.*`) is one implementation of the environment among
others; its names and shape can still move, though never quietly — every
change is in [CHANGELOG.md](../CHANGELOG.md).

## Where things live

| Path | What |
| --- | --- |
| `src/embedbench_device.h` | The frozen interface. |
| `src/embedbench_host.*` | The host Arduino environment. |
| `src/embedbench_internals.h` | Introspection used by the experiments. |
| `devices/` | The device models, as a second Arduino library. |
| `tests/common_env/` | The plain-C++ environment (`nenv`). |
| `tests/conformance/` | The acceptance test for an environment. |

Models live outside `src/` on purpose: Arduino compiles every `.cpp`
under a library's `src/` recursively, so keeping them separate leaves
their build cost with the sketches that use them.

## The conformance kit

`tests/conformance/` is the acceptance test for an environment. It runs
one probe device and one scenario, and both existing implementations
reach the same verdict. A third environment should pass it before
anything is built on top of it.

**Required** (the verdict fails without them):

| Check | What it establishes |
| --- | --- |
| `kCheckNoReentry` | A device is never re-entered while it runs. |
| `kCheckTimeMonotonic` | `advanceTo` never goes backwards. |
| `kCheckNowAgrees` | `nowMicros()` matches the time `advanceTo` was given. |
| `kCheckBorrowedBuffer` | Buffers handed to a device stay valid for that call. |
| `kCheckFrameAccepted` | A well-formed frame is delivered whole. |
| `kCheckFrameOversizeRefused` | An oversized frame is refused, not truncated. |
| `kCheckFormatStable` | A format name always interns to the same id. |
| `kCheckFormatNameLimit` | An over-long format name is refused. |

**Observed but not required** — a conforming environment may or may not
show these, so the probe records them without failing:

`kCheckTimeRepeat` (the contract allows repeated times but does not
demand them), `kCheckAnalogRouted`, `kCheckWakeHonored`,
`kCheckNoteRouted` (the three approved revisions are optional paths; an
environment that does not offer them is still conforming).

**What the kit does not cover**, and would matter for an environment on
real hardware:

- Ordering of effects that land on the same instant. It is deterministic
  in both implementations because each iterates its devices in a fixed
  order, but nothing checks that a third one does.
- The record's overflow behaviour (folding, the truncation notice).
- The diagnostics vocabulary (`diag.unbound` and the rest). Two
  environments that disagree here still both conform, but their traces
  cannot be compared line for line.
- Contract violations on the model side (`reset`, `channelRead` or `dump`
  calling HostPort). The kit checks environments, not models; the model
  side is checked by `tests/device_if/`, which found three real
  violations when it was added.

## Notes for anyone quoting this

- **Model timing constants are annotated.** Most are the real device's
  timings; six are deliberately compressed because the real value would
  make a virtual-clock test span minutes. Each constant says which it is,
  and `tests/device_if/` fails if one does not. Do not draw a timing
  conclusion from a compressed constant.
- **A trace's line count is not the event count.** When the record fills,
  repeating cycles are folded into one round plus a count, so a run can
  offer tens of thousands of events and keep a few dozen lines with
  nothing lost. `stats()` reports `events`, `folded` and `dropped`
  separately.
- **`stats().windows == 0` means `runBegin` was never called**, which is
  a different thing from a run in which nothing happened.
