// A register-map I2C device driven by a table instead of code: the
// registers a part presents when pointed at, their power-on contents, and
// which of them accept writes. The table is data, so a capture of the
// real part can fill it in (devices/tools/trace2regtable.py does that
// from a trace). What a capture cannot say — a status bit that clears
// when a measurement finishes, a FIFO whose read length is device state,
// a line that rises — goes into a derived class through the hooks below.
// The split is deliberate: the table stays data and never grows into a
// rule language (X63).
//
// A "register" here is what one pointer value selects, with a width: a
// 2-byte temperature at 0x00 and a 1-byte config at 0x01 are two entries,
// not three bytes of auto-incrementing memory, because that is how the
// parts in the catalog actually answer. A read returns at most the width
// of the selected register; a write must fit it. Pure C++11.
#pragma once

#include <embedbench_device.h>

// One register: the pointer value that selects it, how many bytes it
// presents or accepts, and its power-on contents (`width` bytes).
struct RegTableEntry {
  uint8_t address;
  uint8_t width;
  const uint8_t* reset;
  uint8_t flags;  // RegTableModel::kWritable | kVolatile | kPartial
};

// A world channel that lands verbatim in one register: the payload
// length must equal the register's width.
struct RegTableChannel {
  uint8_t channel;
  uint8_t address;
};

struct RegTableSpec {
  const RegTableEntry* entries;
  size_t entryCount;
  const RegTableChannel* channels;
  size_t channelCount;
  // A read that does not continue a pointer write answers nothing, the way
  // most sensors and EEPROMs behave.
  bool requireRepeatedStart;
};

class RegTableModel : public ebdev::Device {
 public:
  static const uint8_t kWritable = 0x01;
  // Informational flags the generator sets so a reader of the table knows
  // what the capture showed: the register changed on its own, or its
  // contents were only partly recorded.
  static const uint8_t kVolatile = 0x02;
  static const uint8_t kPartial = 0x04;
  static const size_t kMaxRegisters = 24;
  static const size_t kValueBytes = 96;
  // onRead() result meaning "let the table answer".
  static const size_t kPassThrough = static_cast<size_t>(-1);

  explicit RegTableModel(const RegTableSpec& spec);

  void reset() override;
  uint8_t i2cWrite(const uint8_t* data, size_t len,
                   const ebdev::I2cTransfer& xfer) override;
  size_t i2cRead(uint8_t* data, size_t len,
                 const ebdev::I2cTransfer& xfer) override;
  bool channelWrite(uint8_t channel, const uint8_t* data, size_t len) override;
  size_t channelRead(uint8_t channel, uint8_t* out, size_t cap) override;
  size_t dump(char* out, size_t cap) override;

  // Table access for hooks. All effect-free: none of these call HostPort.
  bool has(uint8_t address) const { return find(address) >= 0; }
  size_t width(uint8_t address) const;
  uint8_t get(uint8_t address, size_t offset = 0) const;
  void set(uint8_t address, size_t offset, uint8_t value);
  void setBytes(uint8_t address, const uint8_t* value, size_t len);
  uint8_t pointer() const { return pointer_; }
  size_t registers() const { return count_; }

 protected:
  // --- Hooks: where the behavior the table cannot say goes -------------
  // Called first for every read of the register at `address` (the current
  // pointer). Return kPassThrough for the table's answer, otherwise the
  // byte count supplied (at most `len`).
  virtual size_t onRead(uint8_t address, uint8_t* out, size_t len) {
    (void)address;
    (void)out;
    (void)len;
    return kPassThrough;
  }
  // Called once the table has stored a write, with what was written.
  virtual void afterWrite(uint8_t address, const uint8_t* value, size_t len) {
    (void)address;
    (void)value;
    (void)len;
  }

 private:
  int find(uint8_t address) const;
  void loadReset();

  const RegTableSpec* spec_;
  size_t count_ = 0;  // entries actually held: fit kMaxRegisters/kValueBytes
  uint16_t offset_[kMaxRegisters];
  uint8_t values_[kValueBytes];
  uint8_t pointer_ = 0;
  uint32_t reads_ = 0;
  uint32_t writes_ = 0;
  uint32_t unmapped_ = 0;
  uint32_t refused_ = 0;
};
