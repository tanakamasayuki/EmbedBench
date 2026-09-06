// EmbedBench — host-side verification for embedded applications.
//
// One include is enough: this brings in the frozen device interface that
// models are written against and the host environment that runs them.
//
//   #include <EmbedBench.h>
//
// Start with docs/GUIDE.md; docs/ADVANCED.md covers writing your own
// device models.
#pragma once

#include "embedbench_version.h"

// The portable boundary between device models and any environment.
// Frozen: see docs/DEVICE_IF_FROZEN.ja.md.
#include "embedbench_device.h"

// This platform's environment. Arduino-host specific.
#include "embedbench_host.h"
