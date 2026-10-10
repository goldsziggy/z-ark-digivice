#include "touch_raw.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks = 0;
void check(bool value, const char* text, int line) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "%d: %s\n", line, text);
        std::exit(1);
    }
}
#define CHECK(x) check((x), #x, __LINE__)
} // namespace

int main() {
    using digivice::display::SpdStatus;
    using digivice::display::classifySpd2010Status;
    using digivice::display::spdDecodedPoint;
    const std::uint8_t busyPoint[]{0x01, static_cast<std::uint8_t>(0x80 | 0x40), 0, 0};
    CHECK(classifySpd2010Status(busyPoint) == SpdStatus::Busy);
    const std::uint8_t bios[]{0, 0x40, 0, 0};
    CHECK(classifySpd2010Status(bios) == SpdStatus::BiosToCpu);
    const std::uint8_t bothModes[]{0, static_cast<std::uint8_t>(0x40 | 0x20), 0, 0};
    CHECK(classifySpd2010Status(bothModes) == SpdStatus::BiosToCpu);
    const std::uint8_t pointMode[]{0, 0x20, 0, 0};
    CHECK(classifySpd2010Status(pointMode) == SpdStatus::PointModeStart);
    const std::uint8_t empty[]{0x00, 0x08, 0, 0};
    CHECK(classifySpd2010Status(empty) == SpdStatus::NoContact);
    const std::uint8_t emptyWithAckBit[]{0x08, 0x08, 0, 0};
    CHECK(classifySpd2010Status(emptyWithAckBit) == SpdStatus::NoContact);
    const std::uint8_t point[]{0x01, 0x08, 10, 0};
    CHECK(classifySpd2010Status(point) == SpdStatus::PointData);
    const std::uint8_t gesture[]{0x02, 0x00, 4, 0};
    CHECK(classifySpd2010Status(gesture) == SpdStatus::PointData);
    const std::uint8_t ack[]{0x08, 0x08, 1, 0};
    CHECK(classifySpd2010Status(ack) == SpdStatus::RunningAck);
    const std::uint8_t idle[]{0, 0, 0, 0};
    CHECK(classifySpd2010Status(idle) == SpdStatus::NoUpdate);

    std::uint8_t packet[10]{};
    packet[4] = 1;
    packet[5] = 0x2a;
    packet[6] = 0x35;
    packet[7] = 0x10;
    packet[8] = 0x40;
    std::uint16_t x = 0, y = 0;
    CHECK(spdDecodedPoint(0x01, packet, 10, x, y) && x == 0x12a && y == 0x035);
    packet[5] = 0xff;
    packet[7] = 0xf0;
    CHECK(!spdDecodedPoint(0x01, packet, 10, x, y));
    CHECK(!spdDecodedPoint(0x01, packet, 9, x, y));
    CHECK(!spdDecodedPoint(0x02, packet, 10, x, y));
    packet[4] = 0x0b;
    packet[5] = 0x2a;
    packet[7] = 0x10;
    CHECK(!spdDecodedPoint(0x01, packet, 10, x, y));
    std::printf("touch raw: %u checks passed\n", checks);
}
