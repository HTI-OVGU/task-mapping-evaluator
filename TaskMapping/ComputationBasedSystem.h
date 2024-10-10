#pragma once

#include "System.h"

class ComputationBasedSystem : public System {
public:
	ComputationBasedSystem(TaskGraph&& t, Platform&& p) : task_graph(std::move(t)), platform(std::move(p)) {};
	void replace_graph(TaskGraph&& g) {
		task_graph = std::move(g);
	}

	Time computation_time_ms(Task* task, Processor const* processor) const {
		Time time = processor->processing_time_ms(task->get_input_size(), task->get_parallelizability())* task->get_complexity();
		if (processor->is_streaming_device()) {
			time /= task->get_streamability();
		}
		return time; 
	}
	Time transaction_time_ms(DataSize const& transfer_size_MB, Device const* dev1, Device const* dev2) const {
		DataRate transfer_rate_MBps = platform.transfer_rate_MBps(dev1, dev2);
		if (transfer_rate_MBps == 0) {
			return std::numeric_limits<Time>::infinity();
		}
		if (transfer_rate_MBps == std::numeric_limits<DataRate>::infinity()) {
			return 0;
		}
		return 1000 * (Time)transfer_size_MB / transfer_rate_MBps;
	}
	bool is_compatible(Task* task, Device const* device) const { 
		if (task->get_edges_in().size() == 0 || task->get_edges_out().size() == 0) {
			return device->get_label() == "CPU" || device->get_label() == "Main_RAM";
		}
		return true; 
	}

	TaskGraph const& get_task_graph() const { return task_graph; }
	Platform const& get_platform() const { return platform; }

private:
	TaskGraph task_graph;
	Platform platform;
};
