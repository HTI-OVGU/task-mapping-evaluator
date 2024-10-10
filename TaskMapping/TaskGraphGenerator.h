#pragma once

#include "TaskGraph.h"
#include "TopologicalSorting.h"

#include <unordered_map>
#include <random>

class TaskPropertyProducer {
    std::default_random_engine gen;
    std::lognormal_distribution<double> lognormal;

public:

    struct TaskProperties {
        ScaleFactor task_complexity;
        Percent parallelizability;
        ScaleFactor streamability;
    };

    TaskPropertyProducer() {
        gen = std::default_random_engine((rand() % 1000));
        //lognormal = std::lognormal_distribution<double>(3.0, 0.5);
        lognormal = std::lognormal_distribution<double>(2.0, 0.5);
    }

    TaskProperties get_properties() {
        return { std::ceil(lognormal(gen)), (Percent)((rand() % 2 == 0) ? 100 : rand() % 101), std::ceil(lognormal(gen)) };
    }
};

TaskGraph generate_random_series_parallel_graph(size_t size = 10, DataSize const& data_in_mb = 1) {

	TaskGraph g;

    Task* src = g.add_node(1, 100, 1, [data_in_mb](std::vector<DataSize> const&) {return data_in_mb;});
	/*Task* snk =*/ g.add_node(1, 100, 1, &DATA_SNK, { src });

	std::unordered_map<Edge*, int> duplicate_edges;
    TaskPropertyProducer tpprod;

	for (size_t i = 0; i < size-2; ++i) {
		std::vector<Edge*> const& edges = g.get_edges();

		while (rand() % 3 < 2) {
		//while (rand() % 2 == 0) {
			// Parallel operation
			Edge* rand_edge = edges[rand() % edges.size()];

			if (duplicate_edges.contains(rand_edge)) {
				++duplicate_edges[rand_edge];
			}
			else {
				duplicate_edges[rand_edge] = 1;
			}
		}

		// Series operation
		Edge* rand_edge = edges[rand() % edges.size()];

        auto properties = tpprod.get_properties();
		/*Task* new_task =*/ g.add_node(properties.task_complexity, properties.parallelizability, properties.streamability, &MAX_PROPAGATION, { rand_edge->get_src() }, { rand_edge->get_snk() });
        //new_task->set_area(properties.streamability * (rand() % 4 + 1));

		if (duplicate_edges.contains(rand_edge) && duplicate_edges.at(rand_edge) > 0) {
			--duplicate_edges[rand_edge];
		}
		else {
			g.delete_edge(rand_edge);
		}
	}

	return g;
}

TaskGraph generate_random_almost_series_parallel_graph(size_t size = 10, DataSize const& data_in_mb = 1, size_t loose_edges = 5) {
    TaskGraph g = generate_random_series_parallel_graph(size, data_in_mb);
    RandomSorting topsort(g, false);
    std::vector<GraphElement> const& sorted_elements = topsort.get_sorted_elements();

    size_t timeout = loose_edges * 10;

    std::vector<Edge> new_edges;

    size_t idx1, idx2;
    for (size_t i = 0; i < loose_edges; ++i) {
        bool invalid = true;
        Task* src = nullptr; 
        Task* snk = nullptr;
        do {
            if (timeout-- == 0) {
                // Stop execution if no new edges to be inserted can be found
                return g;
            }
            idx1 = rand() % sorted_elements.size();
            idx2 = rand() % sorted_elements.size();

            if (idx1 == idx2) {
                continue;
            }

            if (idx1 > idx2) {
                // Assure idx2 > idx1
                size_t tmp = idx2;
                idx2 = idx1;
                idx1 = tmp;
            }

            src = sorted_elements[idx1].get_task();
            snk = sorted_elements[idx2].get_task();

            invalid = false;
            for (Edge* edge : src->get_edges_out()) {
                if (edge->get_snk() == snk) {
                    // Edge already exists
                    invalid = true;
                    break;
                }
            }

            if (!invalid) {
                for (Edge& edge : new_edges) {
                    if (edge.get_src() == src && edge.get_snk() == snk) {
                        // Edge should already be inserted
                        invalid = true;
                        break;
                    }
                }
            }

        } while (invalid);

        assert(src && snk);

        new_edges.push_back({ src, snk });
    }

    for (Edge& edge : new_edges) {
        g.add_edge(edge);
    }

    return g;
}