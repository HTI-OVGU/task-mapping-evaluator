#pragma once

#include "ComputationBasedSystem.h"
#include "TaskGraphGenerator.h"
#include "TaskGraphReader.h"
#include "PlatformGenerator.h"

#include "run_mappings.h"

enum class Configuration { CG, CGF, CGFF };

int nbr_fpgas(Configuration config) {
	switch (config) {
	case Configuration::CG: return 0;
	case Configuration::CGF: return 1;
	case Configuration::CGFF: return 2;
	}
	return 0;
}

std::string label(Configuration config) {
	switch (config) {
	case Configuration::CG: return "CG";
	case Configuration::CGF: return "CGF";
	case Configuration::CGFF: return "CGFF";
	}
	return "";
}

void test_find_small_mapping(int seed, int graph_size, Configuration config, bool (*condition)(TestRun const&)) {
	int RUNS = 1000;
	bool draw_results = true;

	std::cout << "Executing configuration " << label(config) << " with Seed " << seed << std::endl;

	ComputationBasedSystem system(TaskGraph(), create_platform(nbr_fpgas(config)));
	std::vector<TestRun> results;
	for (int i = 0; i < RUNS; ++i) {
		std::cout << "Run " << i + 1 << " of " << RUNS << "...";
		system.replace_graph(generate_random_series_parallel_graph(graph_size));
		results.push_back(TestRun());
		TestRun& test_run = results.back();

        run_default_mappings(system, test_run, draw_results);
		std::cout << "finished!" << std::endl;

		//	Find nice small mapping
		TestRun sorted = test_run;
		std::sort(sorted.begin(), sorted.end(), [](TestResult& first, TestResult& second) {return first.objective < second.objective;});
		if (condition(sorted)) {
			std::cout << "Found at seed " << seed << " with " << i + 1 << " runs!" << std::endl;
			print_results(test_run);
			break;
		}
	}
}

void test_performance(int seed, int graph_size, int runs, std::vector<Configuration> const& configurations) {
	bool draw_results = (runs == 1);
	prepare_files();
	write_log(seed);

	for (auto& config : configurations) {
		std::cout << "Executing configuration " << label(config) << " with Seed " << seed << std::endl;

		ComputationBasedSystem system(TaskGraph(), create_platform(nbr_fpgas(config)));
		draw_hardware_graph(system.get_platform(), "hardware_graph_" + label(config));

		std::vector<TestRun> results;
		for (int i = 0; i < runs; ++i) {
			std::cout << "Run " << i + 1 << " of " << runs << "...";
			system.replace_graph(generate_random_series_parallel_graph(graph_size));
			results.push_back(TestRun());
			TestRun& test_run = results.back();

            run_default_mappings(system, test_run, draw_results);
			std::cout << "finished!" << std::endl;

			if (runs == 1) print_results(test_run);
		}

		results_to_file(results, "statistics.txt", label(config), true);
	}
}

bool get_basefolder(std::string& basefolder) {
	basefolder = "";
	if (std::filesystem::exists("config/folders.cfg")) {
		std::ifstream ifs("config/folders.cfg");
		std::string line;
		while (std::getline(ifs, line)) {
			if (line.find("BENCHMARK_FOLDER") != std::string::npos) {
				size_t pos_start = line.find("\"");
				if (pos_start != std::string::npos) {
					size_t pos_end = line.find("\"", pos_start + 1);
					if (pos_end != std::string::npos) {
						basefolder = line.substr(pos_start + 1, pos_end - pos_start - 1);
					}
				}
				break;
			}
		}
	}

	if (!std::filesystem::exists(basefolder)) {
		std::cout << "Base folder not found. Create config/folders.cfg and put in BENCHMARK_FOLDER=<Your Folder> for this to work." << std::endl;
		return false;
	}
	return true;
}

void test_benchmark_graphs(int seed, int runs, std::vector<Configuration> const& configurations, std::vector<std::string> const& folders, std::vector<MappingType> selection = {}) {
	prepare_files();
	write_log(seed);

	std::string benchmark_base_folder;
	if (!get_basefolder(benchmark_base_folder)) return;

	for (auto& config : configurations) {
		std::cout << "Executing configuration " << label(config) << " with Seed " << seed << std::endl;

		ComputationBasedSystem system(TaskGraph(), create_platform(nbr_fpgas(config)));
		for (std::string const& folder : folders) {
			std::cout << "Processing " << folder << std::endl;

			std::vector<std::pair<int, std::vector<TestRun>>> test_runs;

			std::string folder_name = folder;
			while (folder_name.find("/") != std::string::npos) folder_name = folder_name.substr(folder_name.find("/") + 1);
			std::ofstream ofs("results/" + folder_name + "_out.txt", std::ios_base::app);

			if (!std::filesystem::exists(benchmark_base_folder + folder)) {
				std::cout << "Folder " << folder << " not found." << std::endl;
				continue;
			}
			for (const auto& entry : std::filesystem::directory_iterator(benchmark_base_folder + folder)) {
				size_t size = size_from_json(entry.path().generic_string());
				test_runs.push_back({ size, std::vector<TestRun>() });
				std::vector<TestRun>& results = test_runs.back().second;

				for (int i = 0; i < runs; ++i) {
					std::cout << std::endl << std::setw(50) << std::left << entry.path().filename() << "Run " << i + 1 << " of " << runs << std::endl;

					system.replace_graph(build_from_json(entry.path().generic_string()));
					results.push_back(TestRun());
					TestRun& test_run = results.back();

					run_mappings(system, test_run, selection, false);

					if (runs == 1) print_results(test_run);
				}
			}

			create_plot(test_runs, ofs);
		}
	}
}

void dump_benchmark_graphs(std::vector<std::string> const& paths) {
	prepare_files();

	std::string benchmark_base_folder;
	if (!get_basefolder(benchmark_base_folder)) return;

	for (auto& path : paths) {
		ComputationBasedSystem system(build_from_json(benchmark_base_folder +path), create_platform());
		if (system.get_task_graph().get_tasks().size() == 0) {
			return;
		}

		std::string stripped_path(path);
		stripped_path.erase(std::remove(stripped_path.begin(), stripped_path.end(), '/'), stripped_path.end());
		TestRun dummy;

		run_mapping("Graphdump_" + stripped_path, system, GreedyMapper({ "CPU", "Main_RAM" }), dummy);
		std::cout << "Written Graphdump_" + stripped_path << std::endl;
	}
}

void test_performance_and_export(int seed, std::function<TaskGraph ()> const& graph_gen, std::vector<Configuration> const& configurations, std::ostream& out = std::cout, TestRun* out_run = nullptr) {
	prepare_files();
	write_log(seed);

	for (auto& config : configurations) {
		out << "Executing configuration " << label(config) << " with Seed " << seed << std::endl;

		ComputationBasedSystem system(graph_gen(), create_platform(nbr_fpgas(config)));
		if (system.get_task_graph().get_tasks().size() == 0) {
			return;
		}

		draw_hardware_graph(system.get_platform(), "hardware_graph_" + label(config));

        if (out_run) {
            run_default_mappings(system, *out_run, false, true);
        } else {
            TestRun test_run;
            run_default_mappings(system, test_run, true, true);

            print_results(test_run);
            results_to_file({test_run}, "statistics.txt", label(config), true);
        }
	}
}

void test_size_series(int seed, int from, int step, int to, int runs, std::function<TaskGraph(int)> const& graph_gen, std::vector<Configuration> const& configurations, std::vector<MappingType> selection = {}) {
	prepare_files();
	write_log(seed);

    std::ofstream ofs("results/size_series_out.txt", std::ios_base::app);
	for (auto& config : configurations) {
		std::vector<std::pair<int, std::vector<TestRun>>> test_runs;
		ComputationBasedSystem system(TaskGraph(), create_platform(nbr_fpgas(config)));

		for (int size = from; size <= to; size += step) {
			std::cout << "Executing configuration " << label(config) << " with Seed " << seed << " and size " << size << std::endl;

            test_runs.push_back({ size, std::vector<TestRun>() });
            std::vector<TestRun>& results = test_runs.back().second;
            for (int i = 0; i < runs; ++i) {
                std::cout << "Run " << i + 1 << " of " << runs << "...";
                system.replace_graph(graph_gen(size));
                results.push_back(TestRun());
                TestRun& test_run = results.back();

                if (selection.empty()) {
                    run_default_mappings(system, test_run, false, false);
                    /*if (size <= 20 && size % 5 == 0) {
                        run_mappings(system, test_run, {MappingType::ZhouLiu}, false, false);
                    }*/
                } else {
                    run_mappings(system, test_run, selection, false, false);
                }
                std::cout << "finished!" << std::endl;
            }
		}
        ofs << "\nConfiguration " << label(config) << " (Seed " << seed << ")" << std::endl;
		create_plot(test_runs, ofs);
	}
}

void test_nsgaii_generation_series(int seed, int from, int step, int to, int runs, std::function<TaskGraph()> const& graph_gen, std::vector<Configuration> const& configurations, std::vector<MappingType> additional_selection = {}) {
	prepare_files();
	write_log(seed);

	std::ofstream ofs("results/nsgaii_series_out.txt", std::ios_base::app);

	if (additional_selection.empty()) {
		additional_selection.push_back(MappingType::CPU);
	}

	for (auto& config : configurations) {
		std::vector<std::pair<int, std::vector<TestRun>>> nsgaii_test_runs;
		ComputationBasedSystem system(TaskGraph(), create_platform(nbr_fpgas(config)));

		for (int generations = from; generations <= to; generations += step) {
			nsgaii_test_runs.push_back({ generations, {} });
		}

		for (int run = 0; run < runs; ++run) {

			system.replace_graph(graph_gen());

			TestRun selection_run = TestRun();
			run_mappings(system, selection_run, additional_selection, false, false);

			auto it = nsgaii_test_runs.begin();
			for (int generations = from; generations <= to; generations += step) {
				// Inefficient but consistent with test_size_series
				it->second.push_back(selection_run);
				TestRun& curr_run = it->second.back();

				run_nsgaii_mapping(system, curr_run, generations);
				++it;
			}
		}

		ofs << "\nConfiguration " << label(config) << " (Seed " << seed << ")" << std::endl;
		create_plot(nsgaii_test_runs, ofs);
	}
}