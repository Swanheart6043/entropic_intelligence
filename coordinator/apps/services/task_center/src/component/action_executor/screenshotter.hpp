#include <string>
#include <zmq.hpp>
#include "cloud_gateway_client.hpp"
#include "action_executor.hpp"

class Screenshotter {
public:
    explicit Screenshotter();
    void execute_snapshot(const ActionExecution& action, const std::string& point_name, const std::string& report_uuid);
    std::string format_now_date_to_string();
private:
    zmq::context_t zmq_ctx;
    cloud_gateway::CloudGatewayClient cloud_gateway_;
};
