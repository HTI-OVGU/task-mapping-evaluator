#pragma once

#include "System.h"
#include "Mapping.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <set>
#include <map>

class SubGraph {
	std::vector<Task*> tasks;
	std::vector<Edge*> edges;
	std::unordered_set<Device const*> devices;
	std::vector<Edge*> edges_out;

public:

	void add_task(Task* task, Mapping const& mapping) {
		tasks.push_back(task);

		devices.insert(mapping.get_processor(task));
		devices.insert(mapping.get_mem_in(task));
		devices.insert(mapping.get_mem_out(task));
	}

	void add_edge(Edge* edge) {
		edges.push_back(edge);
	}

	void add_edge_out(Edge* edge) {
		edges_out.push_back(edge);
	}

	std::vector<Task*> get_tasks() const { return tasks; }
	std::vector<Edge*> get_edges() const { return edges; }
	std::vector<Edge*> get_edges_out() const { return edges_out; }
	std::unordered_set<Device const*> get_devices() const { return devices; }
};

class GraphElement {
	enum class PTR_TYPE { TASK, EDGE, SUBGRAPH };

	void* ptr;
	PTR_TYPE type;
public:
	GraphElement(Task* task_ptr) : ptr(task_ptr), type(PTR_TYPE::TASK) {};
	GraphElement(Edge* edge_ptr) : ptr(edge_ptr), type(PTR_TYPE::EDGE) {};
	GraphElement(SubGraph* graph_ptr) : ptr(graph_ptr), type(PTR_TYPE::SUBGRAPH) {};

	Task* get_task() const { return type == PTR_TYPE::TASK ? reinterpret_cast<Task*>(ptr) : nullptr; }
	Edge* get_edge() const { return type == PTR_TYPE::EDGE ? reinterpret_cast<Edge*>(ptr) : nullptr; }
	SubGraph* get_subgraph() const { return type == PTR_TYPE::SUBGRAPH ? reinterpret_cast<SubGraph*>(ptr) : nullptr; }
	void* get_ptr() const { return ptr; }

    void release() { ptr = nullptr; }
};

class TopologicalSorting {
protected:
	bool insert_edges;
	std::vector<GraphElement> sorted_elements;
	std::vector<SubGraph*> subgraphs;
	mutable std::unordered_map<void*, size_t> index_map;
    mutable bool dirty = true;

public:

	std::vector<GraphElement> const& get_sorted_elements() const { return sorted_elements; }
    std::vector<SubGraph*> const& get_subgraphs() const { return subgraphs; }
    bool contains_edges() const { return insert_edges; }

	void compress_streamable_subtrees(Mapping const& mapping, Processor const* streaming_proc) {
		SubGraph* compressable_subgraph;
		do {
			compressable_subgraph = nullptr;

			std::unordered_map<void*, size_t> dependencies;
			for (GraphElement const& elem : sorted_elements) {
				Task* task = elem.get_task();
				if (task) {
					dependencies[task] = task->get_edges_in().size();
				} else if (elem.get_edge()) {
					dependencies[elem.get_ptr()] = 1;
				}
			}

			std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t> > wavefront;
			std::set<size_t> pending;
			std::map<size_t, size_t> pending_tasks;

			size_t elem_idx = 0;
			while (elem_idx < sorted_elements.size()) {
				if (!wavefront.empty() && elem_idx > wavefront.top()) {
					break;
				}

				GraphElement const& elem = sorted_elements[elem_idx];
				if (dependencies[elem.get_ptr()] == 0) {
					Task* task = elem.get_task();
					if (task) {
						if (mapping.get_processor(task) == streaming_proc
                            && task->is_streamable() && mapping.get_mem_in(task)->is_streaming_device() && mapping.get_mem_out(task)->is_streaming_device()) {
											
							while (!wavefront.empty() && elem_idx == wavefront.top()) {
								wavefront.pop();
							}							
							pending.insert(elem_idx);

							for (Edge* edge : task->get_edges_out()) {
								wavefront.push(get_index(edge));
								--dependencies[edge];
							}
						}
						else if (pending.empty()) {
							for (Edge* edge : task->get_edges_out()) {
								--dependencies[edge];
							}
						}
					}

					Edge* edge = elem.get_edge();
					if (edge) {
						if (!wavefront.empty() && elem_idx == wavefront.top()) {
							wavefront.pop();							
							pending.insert(elem_idx);
							pending_tasks[get_index(edge->get_snk())] = elem_idx;

							wavefront.push(get_index(edge->get_snk()));
							--dependencies[edge->get_snk()];
						}
						else if (!compressable_subgraph) {
							--dependencies[edge->get_snk()];
						}
					}					
				}

				SubGraph* subgraph = elem.get_subgraph();
				if (subgraph) {
					if (pending.empty()) {
						for (Edge* edge : subgraph->get_edges_out()) {
							--dependencies[edge];
						}
					}
				}

				++elem_idx;
			}

			if (!pending.empty()) {
				compressable_subgraph = new SubGraph();
				subgraphs.push_back(compressable_subgraph);

				size_t last_idx = *pending.rbegin();
				for (auto it = pending_tasks.rbegin(); it != pending_tasks.rend(); ++it) {
					if (it->first <= last_idx) {
						break;
					}
					last_idx = std::min(last_idx, it->second - 1);
				}

				for (size_t idx : pending) {
					if (idx > last_idx) {
						break;
					}
					GraphElement const& elem = sorted_elements[idx];
					Task* task = elem.get_task();
					if (task) {
						compressable_subgraph->add_task(task, mapping);
						for (Edge* edge : task->get_edges_out()) {
							if (get_index(edge) > last_idx) {
								compressable_subgraph->add_edge_out(edge);
							}
						}
					}
					Edge* edge = elem.get_edge();
					if (edge) {
						compressable_subgraph->add_edge(edge);
					}
				}
			}

			if (compressable_subgraph) {
				std::vector<Task*> const& subgraph_tasks = compressable_subgraph->get_tasks();
				std::vector<Edge*> const& subgraph_edges = compressable_subgraph->get_edges();
				sorted_elements[get_index(subgraph_tasks.front())] = { compressable_subgraph };

				sorted_elements.erase(std::remove_if(
					std::begin(sorted_elements), std::end(sorted_elements),
					[&subgraph_tasks](GraphElement const& elem) -> bool {
						return std::find(subgraph_tasks.begin(), subgraph_tasks.end(), elem.get_ptr()) != subgraph_tasks.end();
					}
				), sorted_elements.end());

				sorted_elements.erase(std::remove_if(
					std::begin(sorted_elements), std::end(sorted_elements),
					[&subgraph_edges](GraphElement const& elem) -> bool {
						return std::find(subgraph_edges.begin(), subgraph_edges.end(), elem.get_ptr()) != subgraph_edges.end();
					}
				), sorted_elements.end());

                dirty = true;
			}

		} while (compressable_subgraph);
	}

    virtual ~TopologicalSorting() {
        for (SubGraph* s : subgraphs) {
            delete s;
        }
    }

protected:
    TopologicalSorting(bool insert_edges = true) : insert_edges(insert_edges)
    {}

    size_t get_index(void* elem) const {
        if (dirty) generate_index();
        return index_map.contains(elem) ? index_map.at(elem) : -1;
    }

	void generate_index() const {
		index_map.clear();
		for (size_t i = 0; i < sorted_elements.size(); ++i) {
			index_map[sorted_elements[i].get_ptr()] = i;
		}
        dirty = false;
	}
};

class RandomSorting : public TopologicalSorting {
    void sort(TaskGraph const& task_graph) {
        std::unordered_map<void*, size_t> dependencies;

        std::vector<GraphElement> next_elements;
        for (Task* src_task : task_graph.get_src()) {
            next_elements.push_back(src_task);
            dependencies[src_task] = 1;
        }

        size_t nbr_elements = next_elements.size();
        while (nbr_elements != 0) {
            int idx = rand() % nbr_elements;
            GraphElement next_element = next_elements[idx];

            if (--dependencies[next_element.get_ptr()] == 0) {
                Task* next_task = next_element.get_task();
                if (next_task) {
                    for (Edge* edge_out : next_task->get_edges_out()) {
                        if (nbr_elements == next_elements.size()) {
                            next_elements.push_back(edge_out);
                        }
                        else {
                            next_elements[nbr_elements] = edge_out;
                        }
                        dependencies[edge_out] = 1;
                        ++nbr_elements;
                    }
                }

                Edge* next_edge = next_element.get_edge();
                if (next_edge) {
                    Task* snk_task = next_edge->get_snk();
                    if (nbr_elements == next_elements.size()) {
                        next_elements.push_back(snk_task);
                    }
                    else {
                        next_elements[nbr_elements] = snk_task;
                    }
                    ++nbr_elements;

                    if (!dependencies.contains(snk_task)) {
                        dependencies[snk_task] = snk_task->get_edges_in().size();
                    }
                }

                if (insert_edges || next_element.get_task()) {
                    sorted_elements.push_back(next_element);
                }
            }

            --nbr_elements;
            next_elements[idx] = next_elements[nbr_elements];
        }
    }
public:
    RandomSorting(TaskGraph const& task_graph, bool insert_edges = true): TopologicalSorting(insert_edges) {
        sort(task_graph);
    }
};

class BFSSorting : public TopologicalSorting {
    void sort(TaskGraph const& task_graph) {
        std::unordered_map<void*, size_t> dependencies;

        std::queue<GraphElement> next_elements;
        for (Task* src_task : task_graph.get_src()) {
            next_elements.push(src_task);
            dependencies[src_task] = 1;
        }

        while (!next_elements.empty()) {
            GraphElement next_element = next_elements.front();
            next_elements.pop();

            if (--dependencies[next_element.get_ptr()] == 0) {
                Task* next_task = next_element.get_task();
                if (next_task) {
                    for (Edge* edge_out : next_task->get_edges_out()) {
                        GraphElement new_element(edge_out);
                        next_elements.push(new_element);
                        dependencies[edge_out] = 1;
                    }
                }

                Edge* next_edge = next_element.get_edge();
                if (next_edge) {
                    Task* snk_task = next_edge->get_snk();
                    GraphElement new_element(snk_task);
                    next_elements.push(new_element);
                    if (!dependencies.contains(snk_task)) {
                        dependencies[snk_task] = snk_task->get_edges_in().size();
                    }
                }

                if (insert_edges || next_element.get_task()) {
                    sorted_elements.push_back(next_element);
                }
            }
        }
    }
public:
    BFSSorting(TaskGraph const& task_graph, bool insert_edges = true): TopologicalSorting(insert_edges) {
        sort(task_graph);
    }
};

class TaskFirstBFSSorting : public TopologicalSorting {
    void sort(TaskGraph const& task_graph) {
        std::unordered_map<Task*, size_t> dependencies;

        std::queue<Task*> next_tasks;
        for (Task* src_task : task_graph.get_src()) {
            next_tasks.push(src_task);
            dependencies[src_task] = 1;
        }

        while (!next_tasks.empty()) {
            Task* next_task = next_tasks.front();
            next_tasks.pop();
            if (--dependencies[next_task] == 0) {
                for (Edge* edge_out : next_task->get_edges_out()) {
                    Task* const snk_task = edge_out->get_snk();
                    next_tasks.push(snk_task);
                    if (!dependencies.contains(snk_task)) {
                        dependencies[snk_task] = snk_task->get_edges_in().size();
                    }
                }
                if (insert_edges) {
                    for (Edge* edge_in : next_task->get_edges_in()) {
                        sorted_elements.push_back(edge_in);
                    }
                }
                sorted_elements.push_back(next_task);
            }
        }
    }
public:
    TaskFirstBFSSorting(TaskGraph const& task_graph, bool insert_edges = true): TopologicalSorting(insert_edges) {
        sort(task_graph);
    }
};

class MappingBasedSorting : public TopologicalSorting {
    void sort(System const& sys, Mapping const& mapping) {
        std::unordered_map<Task*, size_t> dependencies;

        std::vector<Task*> next_tasks;
        std::vector<Edge*> crossing_edges;
        for (Task* src_task : sys.get_task_graph().get_src()) {
            next_tasks.push_back(src_task);
        }

        std::unordered_map<Processor const*, Time> times;
        for (Processor const* proc: sys.get_platform().get_processors()) {
            times[proc] = 0;
        }

        size_t first_task_idx = 0;
        size_t first_edge_idx = 0;
        while (first_task_idx < next_tasks.size() || first_edge_idx < crossing_edges.size()) {
            Time min_time = std::numeric_limits<Time>::max();
            size_t min_idx = std::numeric_limits<size_t>::max();

            for (size_t i=first_task_idx; i < next_tasks.size(); ++i) {
                if (next_tasks[i] && times.at(mapping.get_processor(next_tasks[i])) < min_time) {
                    min_time = times.at(mapping.get_processor(next_tasks[i]));
                    min_idx = i;
                }
            }

            Task* next_task = (first_task_idx < next_tasks.size()) ? next_tasks[min_idx] : nullptr;
            Processor const* const proc = next_task ? mapping.get_processor(next_task) : nullptr;
            Time new_time = (next_task ? min_time + sys.computation_time_ms(next_task, proc) : std::numeric_limits<Time>::max());

            Edge* next_edge = nullptr;
            for (size_t i=first_edge_idx; i < crossing_edges.size(); ++i) {
                if (crossing_edges[i]) {
                    Processor const* const proc_src = mapping.get_processor(crossing_edges[i]->get_src());
                    Processor const* const proc_snk = mapping.get_processor(crossing_edges[i]->get_snk());
                    if (!next_task || ((proc == proc_src || proc == proc_snk) && new_time > std::max(times[proc_src], times[proc_snk]))) {
                        next_edge = crossing_edges[i];
                        crossing_edges[i] = nullptr;

                        while (first_edge_idx < crossing_edges.size() && crossing_edges[first_edge_idx] == nullptr) {
                            ++first_edge_idx;
                        }
                        break;
                    }
                }
            }

            if (next_edge) {
                if (insert_edges) {
                    sorted_elements.push_back(next_edge);
                }
                if (--dependencies[next_edge->get_snk()] == 0) {
                    next_tasks.push_back(next_edge->get_snk());
                }
            } else {
                next_tasks[min_idx] = nullptr;
                times[proc] = new_time;
                while (first_task_idx < next_tasks.size() && next_tasks[first_task_idx] == nullptr) {
                    ++first_task_idx;
                }

                sorted_elements.push_back(next_task);

                for (Edge* edge_out: next_task->get_edges_out()) {
                    Task* const snk_task = edge_out->get_snk();
                    if (!dependencies.contains(snk_task)) {
                        dependencies[snk_task] = snk_task->get_edges_in().size();
                    }

                    if (mapping.get_processor(snk_task) == proc) {
                        if (insert_edges) {
                            sorted_elements.push_back(edge_out);
                        }
                        if (--dependencies[snk_task] == 0) {
                            next_tasks.push_back(snk_task);
                        }
                    } else {
                        crossing_edges.push_back(edge_out);
                    }
                }
            }
        }
    }
public:
    MappingBasedSorting(System const& sys, Mapping const& mapping, bool insert_edges = true): TopologicalSorting(insert_edges) {
        sort(sys, mapping);
    }
};

class CachedSorting : public TopologicalSorting {
public:
    CachedSorting(TopologicalSorting const* sorting) : TopologicalSorting(sorting->contains_edges()) {
        assert(sorting->get_subgraphs().empty()); // Caching with subgraphs not implemented yet
        sorted_elements = sorting->get_sorted_elements();
    }
};

class SortingWrapper : public TopologicalSorting {
public:
    SortingWrapper(std::vector<Task*> sorted_tasks) : TopologicalSorting(true) {
        for (Task* task : sorted_tasks) {
            sorted_elements.push_back(task);
            for (Edge* e : task->get_edges_out()) {
                sorted_elements.push_back(e);
            }
        }
    }
};