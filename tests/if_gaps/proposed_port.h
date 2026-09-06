// A PROPOSAL, not part of the frozen interface: the three paths measured
// in this experiment live here, in the experiment, so the evidence can be
// gathered without touching src/embedbench_device.h. The maintainer
// approves interface additions; until then this is only a sketch of what
// approval would buy.
//
// If approved, these move into HostPort as new virtuals with the same
// defaults (each meaning "this environment does not route it") and the
// revision counter goes up by one.
#pragma once

#include <embedbench_device.h>

namespace proposal {

class ProposedPort : public ebdev::HostPort {
 public:
  // Drive an analog output line of the device: the voltage a sensor
  // presents to an ADC input, in the raw units the application reads.
  virtual bool analogOut(uint8_t line, uint16_t raw) {
    (void)line;
    (void)raw;
    return false;
  }

  // Ask to be advanced at a particular time, so a latency that does not
  // divide by the environment's tick is still served when it is due.
  virtual bool requestWake(uint64_t whenUs) {
    (void)whenUs;
    return false;
  }

  // Report something the device noticed on a path with no return value.
  // Commentary, never an effect.
  virtual bool diagnose(const char* text) {
    (void)text;
    return false;
  }
};

}  // namespace proposal
