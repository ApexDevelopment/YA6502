#pragma once

#include "types.hpp"

class MemoryPage {
public:
    virtual ~MemoryPage() {}

    virtual Byte read_byte(Byte address) const = 0;
    virtual void write_byte(Byte address, Byte value) = 0;
};