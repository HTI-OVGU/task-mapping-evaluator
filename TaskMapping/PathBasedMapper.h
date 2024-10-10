#pragma once

#include "Mapper.h"
#include "MappingUtility.h"
#include <iterator>
#include <optional>
#include <unordered_map>
#include <algorithm>

class Path {
	std::vector<Task*> tasks;
public:
	Path() {}

	Path(Path const& other) = default;

	Path& operator=(Path&& other) noexcept {
		tasks = std::move(other.tasks);
		return *this;
	}

	void add_task(Task* task) {
		tasks.push_back(task);
	}

	bool valid(DevicePair const& dev_pair, System const& sys) {
		for (Task* task : tasks)
		{
			if (!sys.is_compatible(task, dev_pair.get_proc()) || !sys.is_compatible(task, dev_pair.get_mem())) {
				return false;
			}
		}

		return true;
	}

	std::vector<Task*> const& get_tasks() const { return tasks; }
};

class PathTree {
	const Time NO_WEIGHT = -1;

	Time total_weight;
	std::unordered_map<Task*, Time> subgraph_weights;
	std::unordered_set<Task*> start_tasks;
	DevicePair device_pair;
	System const& sys;

public:
	PathTree(std::vector<Task*> const& src_tasks, DevicePair const& device_pair, System const& sys)
		: device_pair(device_pair), sys(sys)
	{
		for (Task* task : src_tasks) {
			start_tasks.insert(task);
		}
		recompute_weights();
	}

	void recompute_weights(Mapping const& mapping = Mapping()) {
		total_weight = 0;
		for (Task* task : start_tasks) {
			compute_weight(task, mapping);
			total_weight = std::max(total_weight, subgraph_weights[task]);
		}
	}

	DevicePair const& get_device_pair() const { return device_pair; }

	bool empty() const { return start_tasks.empty(); }

	Time const& get_weight() const {
		return total_weight;
	}

	Path get_max_path() const {
		Path max_path;
		auto const it = std::max_element(start_tasks.begin(), start_tasks.end(), [this](Task* first, Task* second) {return this->subgraph_weights.at(first) < this->subgraph_weights.at(second);});
		get_max_path_recursive(max_path, *it);
		return max_path;
	}

	Time get_path_weight(Path const& path) const {
		Time weight = 0;
		for (Task* task : path.get_tasks()) {
			std::optional<Task*> max_task = get_max_task(task->get_edges_out());
			weight += subgraph_weights.at(task) - (max_task ? subgraph_weights.at(*max_task) : 0);
		}
		return weight;
	}

	void resolve(Task* task, Mapping const& mapping, bool recompute = true) {
		propagate_no_weight(task);
		for (Edge* next_edge : task->get_edges_out()) {
			if (!mapping.contains(next_edge->get_snk())) {
				subgraph_weights[next_edge->get_snk()] = NO_WEIGHT;
				start_tasks.insert(next_edge->get_snk());
			}
		}
		
		start_tasks.erase(task);
		if (recompute) {
			recompute_weights(mapping);
		}
	}

	void resolve(Path const& path, Mapping const& mapping) {
		if (!path.get_tasks().empty()) {
			for (Task* task : path.get_tasks()) {
				resolve(task, mapping, false);
			}
			resolve(path.get_tasks().front(), mapping);
		}
	}

private:

	void propagate_no_weight(Task* task) {
		if (subgraph_weights.at(task) != NO_WEIGHT) {
			subgraph_weights[task] = NO_WEIGHT;
			for (Edge* prev_edge : task->get_edges_in()) {
				propagate_no_weight(prev_edge->get_src());
			}
		}
	}

	std::optional<Task*> get_max_task(std::vector<Edge*> const& edges) const {
		auto const it = std::max_element(edges.begin(), edges.end(), [this](Edge* first, Edge* second) {return this->subgraph_weights.at(first->get_snk()) < this->subgraph_weights.at(second->get_snk());});
		return (it == edges.end()) ? std::nullopt : std::optional<Task*>{ (*it)->get_snk() };
	}

	void get_max_path_recursive(Path& path, Task* task) const {
		path.add_task(task);

		std::optional<Task*> next_task = get_max_task(task->get_edges_out());
		if (next_task && subgraph_weights.at(*next_task) != NO_WEIGHT) {
			get_max_path_recursive(path, *next_task);
		}
	}

	Time node_weight(Task* task) const {
		return sys.transaction_time_ms(task->get_input_size(), device_pair.get_mem(), device_pair.get_proc())
			+ sys.computation_time_ms(task, device_pair.get_proc())
			+ sys.transaction_time_ms(task->get_output_size(), device_pair.get_proc(), device_pair.get_mem());
	}

	Time edge_weight(Edge* edge, Mapping const& mapping) const {
		Memory const* const src_mem = mapping.contains(edge->get_src()) ? mapping.get_mem_out(edge->get_src()) : device_pair.get_mem();
		Memory const* const snk_mem = mapping.contains(edge->get_snk()) ? mapping.get_mem_in(edge->get_snk()) : device_pair.get_mem();
		return sys.transaction_time_ms(edge->get_src()->get_output_size(), src_mem, snk_mem);
	}

	void compute_weight(Task* task, Mapping const& mapping) {
		Time weight = NO_WEIGHT;
		if (!mapping.contains(task))
		{
			weight = 0;
			Time max_weight_next = 0;
			for (Edge* next_edge : task->get_edges_out()) {
				Task* const child = next_edge->get_snk();
				if (!mapping.contains(child)) {
					if (!subgraph_weights.contains(child) || subgraph_weights.at(child) == NO_WEIGHT) {
						compute_weight(child, mapping);
					}
					max_weight_next = std::max(max_weight_next, subgraph_weights[child] + edge_weight(next_edge, mapping));
				}
				else {
					weight += edge_weight(next_edge, mapping);
				}
			}
			weight += max_weight_next + node_weight(task);

			for (Edge* next_edge : task->get_edges_in()) {
				if (mapping.contains(next_edge->get_src())) {
					weight += edge_weight(next_edge, mapping);
				}
			}
		}
		subgraph_weights[task] = weight;
	}
};

class PathBasedMapper : public Mapper {
public:
	Mapping get_task_mapping(System const& sys) const {
		Mapping mapping;

		std::vector<Task*> const src_tasks(sys.get_task_graph().get_src().begin(), sys.get_task_graph().get_src().end());

		std::vector<PathTree> path_trees;
		add_device_pair(path_trees, "CPU", "Main_RAM", src_tasks, sys);
		add_device_pair(path_trees, "GPU", "GPU_RAM", src_tasks, sys);
		add_device_pair(path_trees, "FPGA", "FPGA_RAM", src_tasks, sys);

		std::unordered_map<Processor const*, Time> total_time;
		std::unordered_map<Processor const*, Area> used_area;
		for (PathTree const& path_tree : path_trees) {
			Processor const* const proc = path_tree.get_device_pair().get_proc();
			used_area[proc] = 0;
			total_time[proc] = 0;
		}

		for (Task* task : sys.get_task_graph().get_tasks()) {
			bool incompatible = false;
			Time min_cost = std::numeric_limits<Time>::infinity();
			DevicePair min_pair;
			for (PathTree const& path_tree : path_trees) {
				DevicePair const& dev_pair = path_tree.get_device_pair();
				Processor const* proc = dev_pair.get_proc();
				Time node_cost = single_node_cost(task, dev_pair, sys) + total_time[proc];
				if (!sys.is_compatible(task, proc) || !sys.is_compatible(task, dev_pair.get_mem())) {
					incompatible = true;
				}
				else if (node_cost < min_cost && task->get_area_requirement() + used_area[proc] <= proc->get_maximum_capacity()) {
					min_cost = node_cost;
					min_pair = dev_pair;
				}
			}
			if (incompatible) {
				Processor const* proc = min_pair.get_proc();
				mapping.map(task, proc, min_pair.get_mem(), min_pair.get_mem());
				total_time[proc] = min_cost;
				used_area[proc] += task->get_area_requirement();

				for (PathTree& path_tree : path_trees) {
					path_tree.resolve(task, mapping);
				}
			}
		}

		while (!path_trees.empty() && !path_trees.front().empty()) {
			auto max_tree_it = std::max_element(path_trees.begin(), path_trees.end(), [](auto& first_tree, auto& second_tree) {return first_tree.get_weight() < second_tree.get_weight();});
			Path const max_path = max_tree_it->get_max_path();

			Area path_area = 0;
			for (Task* task : max_path.get_tasks()) {
				path_area += task->get_area_requirement();
			}

			Time min_max_path_weight = std::numeric_limits<Time>::infinity();
			DevicePair min_max_dev_pair;
			for (PathTree const& path_tree : path_trees) {
				DevicePair const& dev_pair = path_tree.get_device_pair();
				Time path_weight = path_tree.get_path_weight(max_path) + total_time[dev_pair.get_proc()];
				if (path_weight < min_max_path_weight && path_area + used_area[dev_pair.get_proc()] <= dev_pair.get_proc()->get_maximum_capacity()) {
					min_max_dev_pair = dev_pair;
					min_max_path_weight = path_weight;
				}
			}

			used_area[min_max_dev_pair.get_proc()] += path_area;
			total_time[min_max_dev_pair.get_proc()] = min_max_path_weight;

			for (Task* task : max_path.get_tasks()) {
				mapping.map(task, min_max_dev_pair.get_proc(), min_max_dev_pair.get_mem(), min_max_dev_pair.get_mem());
			}

			for (auto& path_tree : path_trees) {
				path_tree.resolve(max_path, mapping);
			}
		}

		return mapping;
	}

private:
	Time single_node_cost(Task* task, DevicePair const& dev_pair, System const& sys) const {
		return sys.transaction_time_ms(task->get_input_size(), dev_pair.get_mem(), dev_pair.get_proc())
			+ sys.computation_time_ms(task, dev_pair.get_proc())
			+ sys.transaction_time_ms(task->get_output_size(), dev_pair.get_proc(), dev_pair.get_mem());
	}

	void add_device_pair(std::vector<PathTree>& path_trees, std::string const& proc_label, std::string const& mem_label, std::vector<Task*> const& src_tasks, System const& sys) const {
		DevicePair const dev_pair(proc_label, mem_label, sys.get_platform());

		if (dev_pair.valid()) {
			path_trees.push_back({ src_tasks, dev_pair, sys });
		}
	}
};