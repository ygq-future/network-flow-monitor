#ifndef NETWORK_FLOW_MONITOR_PROCESS_APPLICATION_IDENTITY_H
#define NETWORK_FLOW_MONITOR_PROCESS_APPLICATION_IDENTITY_H

#include <string>

namespace nfm::process {

using ApplicationId = std::string;

struct ApplicationMetadata {
    std::string display_name;
    std::string icon_key;

    bool operator==(const ApplicationMetadata&) const = default;
};

} // namespace nfm::process

#endif // NETWORK_FLOW_MONITOR_PROCESS_APPLICATION_IDENTITY_H
