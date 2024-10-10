#pragma once

#include "types.h"
#include "GUID.h"

#include <vector>
#include <unordered_set>
#include <cassert>
#include <string>
#include <functional>

class Task;
class TaskGraph;

class Edge {
public:
	Edge(Task* src, Task* snk) : src(src), snk(snk) {}

	Task* get_src() const { return src;	};
	Task* get_snk() const { return snk; };
private:
	Task* src;
	Task* snk;
};

DataSize SUMMED_PROPAGATION(std::vector<DataSize> const& data_in);
DataSize MAX_PROPAGATION(std::vector<DataSize> const& data_in);
DataSize AVERAGE_PROPAGATION(std::vector<DataSize> const& data_in);
DataSize DATA_SRC(std::vector<DataSize> const&);
DataSize DATA_SNK(std::vector<DataSize> const&);

class Task {
	friend class TaskGraph;
public:
    //typedef DataSize (*SizeFuncPtr) (std::vector<DataSize> const&);
    typedef std::function<DataSize (std::vector<DataSize> const&)> SizeFuncPtr;
	//std::vector<Task*> const& get_predecessors() const { return predecessors; };
	//std::vector<Task*> const& get_successors() const { return successors; };	
	std::vector<Edge*> const& get_edges_in() const { return edges_in; };
	std::vector<Edge*> const& get_edges_out() const { return edges_out; };

	Task(ScaleFactor const& complexity, Percent const& parallelizability, ScaleFactor const& streamability, SizeFuncPtr const& size_func) :
		complexity(complexity), 
		parallelizability(parallelizability), 
		streamability(streamability),
		area(0),
		size_func(size_func)
	{
        GUID = generate_GUID();
    }

    void set_size_func(SizeFuncPtr const& func) {size_func = func; dirty = true;}
	void set_area(Area const& area) {
		this->area = area;
	}

	DataSize const& get_input_size() const { 
		if (dirty) {
			compute_size();
		}
		return input_size; 
	}
	DataSize const& get_output_size() const {
		if (dirty) {
			compute_size();
		}
		return output_size; 
	}

	Percent const& get_parallelizability() const { return parallelizability; }
	ScaleFactor const& get_complexity() const { return complexity; }
	ScaleFactor const& get_streamability() const { return streamability; }
	Area get_area_requirement() const { return area == 0 ? complexity : area; }

	std::string get_label() const {
		return /*std::to_string(task->get_edges_in().size()) + std::to_string(task->get_edges_out().size()) + "_" +*/
			std::to_string((long)get_parallelizability()) + "_" + std::to_string((long)get_complexity()) + "_" + std::to_string(GUID);
	}

	bool is_streamable() const { return streamability > 1; }

	std::vector<Task*> compute_successors() const {
		std::vector<Task*> successors;
		for (Edge* e : edges_out) {
			if (std::find(successors.begin(), successors.end(), e->get_snk()) == successors.end()) {
				successors.push_back(e->get_snk());
			}
		}
		return successors;
	}

private:
	void compute_size() const;
	void add_outgoing_edge(Edge* edge_out);
	void delete_outgoing_edge(Edge* edge_out);

	std::vector<Edge*> edges_in;
	std::vector<Edge*> edges_out;

	mutable DataSize input_size = -1;
	mutable DataSize output_size = -1;

	mutable bool dirty = true;

    ScaleFactor complexity;
	Percent parallelizability;
	ScaleFactor streamability;
	Area area;

	SizeFuncPtr size_func;

    unsigned GUID;
};

class TaskGraph {
public:
	~TaskGraph();
	TaskGraph() = default;

	TaskGraph(TaskGraph&& other) noexcept :
		src_nodes(std::move(other.src_nodes)),
		snk_nodes(std::move(other.snk_nodes)),
		tasks(std::move(other.tasks)),
		edges(std::move(other.edges))
	{}

	void operator=(TaskGraph&& other) noexcept {
		clear();
		src_nodes = std::move(other.src_nodes);
		snk_nodes = std::move(other.snk_nodes);
		tasks = std::move(other.tasks);
		edges = std::move(other.edges);
	}

	std::unordered_set<Task*> const& get_src() const { return src_nodes; };
	std::unordered_set<Task*> const& get_snk() const { return snk_nodes; };

	Task* add_node(ScaleFactor const& complexity = 1, Percent const& parallelizability = 0, ScaleFactor const& streamability = 1, Task::SizeFuncPtr const& size_func = &SUMMED_PROPAGATION, std::vector<Task*> predecessors = {}, std::vector<Task*> successors = {});
	void add_edge(Edge const& edge);
	void add_edge(Task* src, Task* snk);
	void delete_edge(Edge* edge);
	void delete_edge(Task* src, Task* snk);

	std::vector<Task*> const& get_tasks() const { return tasks; };
	std::vector<Edge*> const& get_edges() const { return edges; };

private:
	void clear() {
		for (Task* task_ptr : tasks) {
			delete task_ptr;
		}

		for (Edge* edge_ptr : edges) {
			delete edge_ptr;
		}

		tasks.clear();
		edges.clear();
		src_nodes.clear();
		snk_nodes.clear();
	}

	std::unordered_set<Task*> src_nodes;
	std::unordered_set<Task*> snk_nodes;

	std::vector<Task*> tasks;
	std::vector<Edge*> edges;
};