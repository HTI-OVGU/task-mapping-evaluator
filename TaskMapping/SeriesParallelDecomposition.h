#pragma once
#include "TaskGraph.h"
#include "TopologicalSorting.h"

#include "SafeBoostHeaders.h"
#include <unordered_map>
#include <unordered_set>
#include <fstream>

enum class SeriesParallelOperationType { SERIES, PARALLEL, EDGE };

class SeriesParallelOperation {
	SeriesParallelOperationType type;
	std::vector<SeriesParallelOperation*> elements;
	Task* front;
	Task* back;
	size_t parallel_out = 0; // Number of edges leading to the back node from inside the operation

	void push_operation(SeriesParallelOperation* op) {
		if (op->get_type() == type) {
			for (SeriesParallelOperation* op_elem : op->elements) {
				insert_element(op_elem);
			}
			op->elements.clear();
			delete op;
		}
		else {
			insert_element(op);
		}
	}

	void insert_element(SeriesParallelOperation* op) {
		elements.push_back(op);
		if (type == SeriesParallelOperationType::PARALLEL) {
			parallel_out += op->get_parallel_out();
		}
		else { // Series operation, pushed element is last element
			parallel_out = op->get_parallel_out();
		}
	}
public:
	SeriesParallelOperation(SeriesParallelOperation* first, SeriesParallelOperation* second, SeriesParallelOperationType type) : type(type) {
		front = first->get_front();
		back = second->get_back();

		push_operation(first);
		push_operation(second);
	}

	SeriesParallelOperation(std::vector<SeriesParallelOperation*> ops) : type(SeriesParallelOperationType::PARALLEL) {
		assert(ops.size() > 0);

		front = ops.front()->get_front();
		back = ops.front()->get_back();

		for (SeriesParallelOperation* op : ops) {
			push_operation(op);
		}
	}

	SeriesParallelOperation(Task* from, Task* to) : type(SeriesParallelOperationType::EDGE) {
		front = from;
		back = to;
		parallel_out = 1;
	}

	~SeriesParallelOperation() {
		for (SeriesParallelOperation* element : elements) {
			delete element;
		}
	}

	void insert_child(SeriesParallelOperation* op) {
		elements.push_back(op);
		back = op->get_back();
	}

	void set_type(SeriesParallelOperationType op_type) {
		type = op_type;
	}

	Task* get_front() const {
		return front;
	}

	Task* get_back() const {
		return back;
	}

	size_t get_parallel_out() const {
		return parallel_out;
	}

	std::vector<SeriesParallelOperation*> const& get_elements() const { return elements; }

	SeriesParallelOperationType get_type() const { return type; }
};

class SeriesParallelDecomposition {
	std::vector<SeriesParallelOperation*> inner_nodes;
	std::vector<SeriesParallelOperation*> leaves;

	std::unordered_set<SeriesParallelOperation*> root_set;

	std::unordered_map<Task*, int> missing_inputs;
public:

	SeriesParallelDecomposition(TaskGraph const& task_graph) {
		create_tree(task_graph);
	}

	~SeriesParallelDecomposition() {
		for (SeriesParallelOperation* forest_root : root_set) {
			delete forest_root;
		}
	}

	std::vector<SeriesParallelOperation*> const& get_inner_nodes() const { return inner_nodes; }

	void draw(std::string const& output_filename) {
		struct vertex_info {
			std::string label;

			void set_label(SeriesParallelOperation* op) {
				switch (op->get_type()) {
				case SeriesParallelOperationType::EDGE:
					label = "EDGE";
					break;
				case SeriesParallelOperationType::SERIES:
					label = "SERIES";
					break;
				case SeriesParallelOperationType::PARALLEL:
					label = "PARALLEL";
					break;
				}
				std::string front_label = op->get_front() ? op->get_front()->get_label() : "Start";
				std::string back_label = op->get_back() ? op->get_back()->get_label() : "End";
				label += "\n" + front_label + "\n-- " + back_label;
			}
		};

		boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, vertex_info> g(leaves.size() + inner_nodes.size());

		std::unordered_map<SeriesParallelOperation*, size_t> op_idx;
		size_t i = 0;
		for (auto& op : leaves) {
			g[i].set_label(op);
			op_idx[op] = i++;
		}
		for (auto& op : inner_nodes) {
			g[i].set_label(op);
			op_idx[op] = i++;
		}

		for (auto& op : inner_nodes) {
			for (auto& element : op->get_elements()) {
				boost::add_edge(op_idx[op], op_idx[element], g);
			}
		}

		boost::dynamic_properties dp;
		dp.property("label", boost::get(&vertex_info::label, g));
		dp.property("node_id", boost::get(boost::vertex_index, g));

		std::string output_path = "results/" + output_filename;
		std::ofstream outf(output_path + ".gv");
		boost::write_graphviz_dp(outf, g, dp);

		std::string command = "dot -Tpdf " + output_path + ".gv > " + output_path + ".pdf";
		[[maybe_unused]] int err = std::system(command.c_str());
	}

private:

	SeriesParallelOperation* create_leaf(Task* src, Task* snk) {
		SeriesParallelOperation* op = new SeriesParallelOperation(src, snk);
		leaves.push_back(op);
		return op;
	}

	SeriesParallelOperation* create_parallel(std::vector<Edge*> const& children) {

		std::unordered_map<Task*, std::vector<SeriesParallelOperation*> > wavefront;
		for (Edge* child_edge : children) {
			wavefront[child_edge->get_snk()].push_back(create_leaf(child_edge->get_src(), child_edge->get_snk()));
		}

		while (true) {

			bool change = true;
			while (change) {
				change = false;

				if (wavefront.size() == 1 && wavefront.begin()->second.size() == 1) {
					SeriesParallelOperation* root = wavefront.begin()->second.front();
					return root;
				}

				for (auto& wfpair : wavefront) {
					if (wfpair.second.size() > 1) {
						// Two operations with the same front and back
						SeriesParallelOperation* op = new SeriesParallelOperation(wfpair.second);
						wfpair.second.clear();
						wfpair.second.push_back(op);
						change = true;
					}
					else {
						SeriesParallelOperation* edgeop = grow_operation(wfpair.second.front());

						if (edgeop != wfpair.second.front()) {
							wavefront.erase(wfpair.first);

							wavefront[edgeop->get_back()].push_back(edgeop);
							change = true;
							break;
						}
					}
				}
			}

			// Graph is not series-parallel. Extract one of the faulty operations.
			auto it = wavefront.begin();
			assert(it->second.size() == 1);

			auto faulty_op = it->second.front();
			root_set.insert(faulty_op);
			if (missing_inputs.contains(faulty_op->get_back())) {
				missing_inputs[faulty_op->get_back()] += faulty_op->get_parallel_out();
			}
			else {
				missing_inputs[faulty_op->get_back()] = faulty_op->get_parallel_out();
			}
			wavefront.erase(it);
		}

		// cannot be reached
		return nullptr;
	}

	SeriesParallelOperation* grow_operation(SeriesParallelOperation* op) {
		Task* next = op->get_back();
		while (next && next->get_edges_in().size()-(missing_inputs.contains(next) ? missing_inputs.at(next) : 0) <= op->get_parallel_out()) { // Could be root
			if (next->get_edges_out().size() == 0) {
				op = create_series(op, create_leaf(next, nullptr));
				next = nullptr;
			}
			else if (next->get_edges_out().size() == 1) {
				op = create_series(op, create_leaf(next, next->get_edges_out()[0]->get_snk()));
				next = op->get_back();
			}
			else {
				SeriesParallelOperation* parop = create_parallel(next->get_edges_out());
				op = create_series(op, parop);
				next = op->get_back();
			}
		}
		return op;
	}

	SeriesParallelOperation* create_series(SeriesParallelOperation* op, SeriesParallelOperation* child) {
		return new SeriesParallelOperation(op, child, SeriesParallelOperationType::SERIES);
	}

	bool create_tree(TaskGraph const& task_graph) {
		SeriesParallelOperation* root;
		if (task_graph.get_src().size() == 1) {
			root = grow_operation(create_leaf(nullptr,*task_graph.get_src().begin()));
		}
		else {
			std::vector<Edge*> start_edges;
			for (Task* task : task_graph.get_src()) {
				start_edges.push_back(new Edge(nullptr, task));
			}
			root = create_parallel(start_edges);

			for (Edge* e : start_edges) {
				delete e;
			}

			if (root->get_back() != nullptr) {
				root = grow_operation(root);
			}
		}

		assert(root->get_back() == nullptr);

		root_set.insert(root);
		for (auto& rootop : root_set) {
			add_inner_nodes(rootop);
		}
		return true;
	}

	void add_inner_nodes(SeriesParallelOperation* root) {
		if (root->get_type() != SeriesParallelOperationType::EDGE) {
			inner_nodes.push_back(root);
			for (SeriesParallelOperation* element : root->get_elements()) {
				add_inner_nodes(element);
			}
		}
	}
};