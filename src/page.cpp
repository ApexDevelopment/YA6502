#include "types.hpp"
#include "page.hpp"

class Page : public MemoryPage {
public:
    Page() : data() {}

    Byte read_byte(Byte address) const {
        return data[address];
    }

    void write_byte(Byte address, Byte value) {
        data[address] = value;
    }

private:
    Byte data[256];
};
