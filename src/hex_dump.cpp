#include "hex_dump.hpp"

#include <iomanip>
#include <sstream>

namespace cn {

std::string hex_dump(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); i += 16) {
        out << std::setw(4) << i << "  ";
        for (std::size_t j = 0; j < 16; ++j) {
            if (i + j < bytes.size()) out << std::setw(2) << static_cast<unsigned>(bytes[i + j]) << ' ';
            else out << "   ";
        }
        out << " |";
        for (std::size_t j = 0; j < 16 && i + j < bytes.size(); ++j) {
            const unsigned char c = bytes[i + j];
            out << (c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
        }
        out << "|\n";
    }
    return out.str();
}

} // namespace cn
