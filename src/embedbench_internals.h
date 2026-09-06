// EmbedBench host environment — introspection for the experiments.
//
// These report the environment's own limits and record shape. They exist
// because the experiment ledger measures them; an application-side test
// asserting on device behaviour does not need any of them, so they are
// kept out of embedbench_host.h rather than sitting alongside the
// functions that are actually part of writing a test.
//
// Include this only from an experiment that measures the environment.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ebhost {

// How many observers of the event stream can be registered at once.
size_t listenerCapacity();

// This environment's per-call frame capacity in bits (all buses). An
// oversized frameTx/frameRx is rejected whole with a diagnostic event.
uint32_t frameCapacityBits();

// How many effects (interrupts, application frame deliveries) raised
// while a device runs can be held for delivery after it returns; beyond
// this an effect is diagnosed and dropped, never delivered re-entrantly.
size_t deferralCapacity();

// The earliest outstanding wake request, or 0 when there is none.
uint64_t pendingWakeUs();

// Record shape and volume, as measured in the ledger (X10, X22).
size_t eventCount();
size_t respLineCount();
size_t eventBytes();

}  // namespace ebhost
