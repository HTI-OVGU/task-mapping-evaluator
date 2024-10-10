#include "DrawGraph.h"
#include "SafeBoostHeaders.h"

enum class OutMode { SECONDS, MILLISECONDS };

const OutMode OUTPUT_MODE = OutMode::MILLISECONDS;

void draw_graph(TaskGraph const& task_graph, Mapping const& mapping, std::string const& output_filename, EvaluationLog const& log) {
	static std::string time_label = (OUTPUT_MODE == OutMode::SECONDS) ? "s" : "ms";
	static unsigned time_divisor = (OUTPUT_MODE == OutMode::SECONDS) ? 1000 : 1;

	struct vertex_info {
		std::string label;

		void set_label(Task* task, Mapping const& mapping, EvaluationLog const& log) {
			label = "";

			if (mapping.contains(task)) {
				std::string mem_in_label = mapping.get_mem_in(task)->get_label();
				std::string mem_out_label = mapping.get_mem_out(task)->get_label();
				label += mapping.get_processor(task)->get_label() + "\n" + mem_in_label;
				if (mem_in_label != mem_out_label) {
					label += " -- " + mapping.get_mem_out(task)->get_label();
				}
			}
			std::stringstream par;
			par << std::fixed << std::setprecision(2) << (double)task->get_parallelizability()/100;
			label += "\np=" + par.str() + ", c=" + std::to_string((long)task->get_complexity()) + ", s=" + std::to_string((long)task->get_streamability());
			//label += "\n" + std::to_string(task->get_input_size() / 1000) + "GB / " + std::to_string((long)task->get_complexity()) 
			//	+ " / " + std::to_string(task->get_parallelizability()) + "% / " + std::to_string((long)task->get_streamability());
			if (log.contains(task)) {
				label += "\n" + std::to_string((long)(log.start_time_ms(task)/time_divisor)) + time_label + " -- " + std::to_string((long)(log.end_time_ms(task)/time_divisor)) + time_label;
			}
		}
	};

	struct edge_info {
		std::string label;

		edge_info(Edge* edge, EvaluationLog const& log) { set_label(edge, log); }

		void set_label(Edge* edge, EvaluationLog const& log) {
			label = "";

			if (log.contains(edge)) {
				Time const start_time_ms = log.start_time_ms(edge);
				Time const end_time_ms = log.end_time_ms(edge);
				label = " " + std::to_string((long)(start_time_ms / time_divisor)) + time_label;
				if (start_time_ms != end_time_ms) {
					label += " -- " + std::to_string((long)(end_time_ms / time_divisor)) + time_label;
				}
			}
		}
	};

	boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, vertex_info, edge_info> g(task_graph.get_tasks().size());

	std::unordered_map<Task*, int> task_idx;
	int i = 0;
	for (auto& task : task_graph.get_tasks()) {
		g[i].set_label(task, mapping, log);
		task_idx[task] = i++;
	}

	for (auto& edge : task_graph.get_edges()) {
		boost::add_edge(task_idx[edge->get_src()], task_idx[edge->get_snk()], { edge, log }, g);
	}

	boost::dynamic_properties dp;
	dp.property("label", boost::get(&vertex_info::label, g));
	dp.property("label", boost::get(&edge_info::label, g));
	dp.property("node_id", boost::get(boost::vertex_index, g));

	std::string output_path = "results/" + output_filename;
	std::ofstream outf(output_path + ".gv");
	boost::write_graphviz_dp(outf, g, dp);

	std::string command = "dot -Tpdf " + output_path + ".gv > " + output_path + ".pdf";
    [[maybe_unused]] int err = system(command.c_str());
}

void draw_hardware_graph(Platform const& platform, std::string const& output_filename) {
	struct vertex_info {
		std::string label;

		void set_label(Device const* dev) {
			label = dev->get_label() + "\n" + std::to_string((long)dev->data_movement_rate_MBps()) + " MB/s";
		}
	};

	struct edge_info {
		std::string label;

		edge_info(DataRate const& transfer_rate) {
			label = std::to_string((long)transfer_rate) + " MB/s";
		}
	};

	std::vector<Device*> devices;
	devices.insert(devices.end(), platform.get_memories().begin(), platform.get_memories().end());
	devices.insert(devices.end(), platform.get_processors().begin(), platform.get_processors().end());

	boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS, vertex_info, edge_info > g(devices.size());


	std::unordered_map<Device const*, int> device_idx;
	int i = 0;
	for (Device const* device : devices) {
		g[i].set_label(device);
		device_idx[device] = i++;
	}

	for (Device const* dev1 : devices) {
		for (Device const* dev2 : devices) {
			if (device_idx[dev2] > device_idx[dev1]) {
				DataRate const transfer_rate = platform.transfer_rate_MBps(dev1, dev2);
				if (transfer_rate > 0 && transfer_rate < std::numeric_limits<DataRate>::infinity()) {
					boost::add_edge(device_idx[dev1], device_idx[dev2], { transfer_rate }, g);
				}
			}
		}
	}

	boost::dynamic_properties dp;
	dp.property("label", boost::get(&vertex_info::label, g));
	dp.property("node_id", boost::get(boost::vertex_index, g));
	dp.property("label", boost::get(&edge_info::label, g));

	std::string output_path = "results/" + output_filename;
	std::ofstream outf(output_path + ".gv");
	boost::write_graphviz_dp(outf, g, dp);

	std::string command = "dot -Tpdf " + output_path + ".gv > " + output_path + ".pdf";
	[[maybe_unused]] int err = system(command.c_str());
}
