#include "FrameContinuity.h"
#include <stdexcept>

void check(bool ok) { if (!ok) throw std::runtime_error("frame gap classification mismatch"); }
int main() {
    FrameContinuity stream;
    check(stream.accept("2026-10-02 10:00:00.000", 16.7, "Hardware: Independent Flip"));
    check(stream.accept("2026-10-02 10:00:00.017", 16.7, "Hardware: Independent Flip"));
    check(!stream.accept("2026-10-02 10:00:42.600", 42583.0, "Composed: Flip"));
    check(stream.accept("2026-10-02 10:00:42.617", 16.7, "Composed: Flip"));
    check(!stream.accept("2026-10-02 10:00:40.000", 16.7, "Composed: Flip"));
    check(stream.gapCount() == 2);
}
