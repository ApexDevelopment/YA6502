#include <algorithm>
#include <array>
#include "types.hpp"
#include "page.hpp"

class RAMPage : public MemoryPage {
public:
    RAMPage() {
        std::fill(data.begin(), data.end(), 0);
    }

    Byte read_byte(Byte address) const {
        return data[address];
    }

    void write_byte(Byte address, Byte value) {
        data[address] = value;
    }

private:
    std::array<Byte, 256> data;;
};
