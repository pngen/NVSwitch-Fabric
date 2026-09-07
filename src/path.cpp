#include "nvswitch_fabric/path.h"
#include <sstream>
namespace nvswitch_fabric {
std::string FabricPath::canonicalKey() const {
    std::ostringstream os;
    os << "path[src=" << source.get() << ",dst=" << destination.get() << "]";
    os << "{sw=[";
    for (size_t i = 0; i < hops.size(); ++i) {
        if (i) os << ",";
        os << "(" << hops[i].sw.get() << ";in=" << (hops[i].inPort.isUnknown()?-1:(long)hops[i].inPort.index)
           << ";out=" << (hops[i].outPort.isUnknown()?-1:(long)hops[i].outPort.index);
        os << ";";
        if (hops[i].exitToDevice) os << "dev";
        else if (hops[i].exitToSwitch) os << "sw";
        os << ")";
    }
    os << "]}";
    return os.str();
}
} // namespace nvswitch_fabric
