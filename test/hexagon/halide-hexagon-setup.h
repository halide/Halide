using namespace Halide;
void setupHexagonTarget(Target &target) {
        target.os = Target::OSUnknown; // The operating system
        target.arch = Target::Hexagon;   // The CPU architecture
        target.bits = 32;            // The bit-width of the architecture
        target.set_feature(Target::HVX);
}
