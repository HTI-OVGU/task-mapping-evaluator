#pragma once
#include "types.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <string>

inline std::tm localtime_xp(std::time_t timer)
{
	std::tm bt{};
#if defined(__unix__)
	localtime_r(&timer, &bt);
#elif defined(_MSC_VER)
	localtime_s(&bt, &timer);
#else
	static std::mutex mtx;
	std::lock_guard<std::mutex> lock(mtx);
	bt = *std::localtime(&timer);
#endif
	return bt;
}

// default = "YYYY-MM-DD HH:MM:SS"
inline std::string time_stamp(const std::string& fmt = "%F %T")
{
	auto bt = localtime_xp(std::time(0));
	char buf[64];
	return { buf, std::strftime(buf, sizeof(buf), fmt.c_str(), &bt) };
}

struct TestResult {
	std::string label;
	Time objective;
	std::chrono::milliseconds runtime_ms;
    bool timeout;
};

typedef std::vector<TestResult> TestRun;

struct Statistic {
    std::string label;
    int nbr_winner = 0;
    int nbr_impr = 0;
    int nbr_worsen = 0;
    int nbr_equal = 0;
    int nbr_timeout = 0;
    size_t total_runs = 0;
    double total_impr = 0;
    double total_rel_impr = 0;
    double total_rel_positive_impr = 0;
    double min_impr = std::numeric_limits<double>::infinity();
    double max_impr = -std::numeric_limits<double>::infinity();
    double total_time_ms = 0;
    double total_objective = 0;
    double total_ref_objective = 0;

    void update(double result, double ref_result) {
        double impr = ref_result - result;
        if (impr > 0) {
            ++nbr_impr;
        }
        else if (impr == 0) {
            ++nbr_equal;
        }
        else {
            ++nbr_worsen;
        }

        total_impr += impr;
        total_rel_impr += impr / ref_result;
        if (impr > 0) {
            total_rel_positive_impr += impr / ref_result;
        }
        min_impr = std::min(min_impr, impr / ref_result);
        max_impr = std::max(max_impr, impr / ref_result);

        total_objective += result;
        total_ref_objective += ref_result;

        ++total_runs;
    }
};

void print_results(TestRun const& test_run, std::ostream& out = std::cout) {
	for (TestResult const& result : test_run) {
		out << std::left << std::setw(35) << result.label + " finished." << "Time spent : " << std::right << std::setw(4) << result.runtime_ms.count() << " ms, Objective value : " << result.objective / 1000 << "s" << std::endl;
	}
	TestRun sorted = test_run;
	std::sort(sorted.begin(), sorted.end(), [](TestResult& first, TestResult& second) {return first.objective < second.objective;});

	out << std::endl << "Order:";
	for (TestResult const& result : sorted) {
		out << " " << result.label;
	}
	out << std::endl << std::endl;

}

void prepare_files() {
	if (!std::filesystem::exists("results/")) {
		std::filesystem::create_directory("results");
	}
	std::filesystem::remove("results/statistics.txt");

	if (!std::filesystem::exists("export/")) {
		std::filesystem::create_directory("export");
	}

	if (!std::filesystem::exists("export/kernels/")) {
		std::filesystem::create_directory("export/kernels");
	}
}

void write_log(int seed) {	
	std::ofstream ofs("results/seeds.log", std::ios_base::app);
	ofs << time_stamp() << " Seed: " << seed << std::endl;
}

std::vector<Statistic> create_statistics(std::vector<TestRun> const& results) {
    std::vector<Statistic> statistics(results.front().size());

    size_t CPU_idx = -1;
    for (size_t i = 0; i < results.front().size(); ++i) {
        statistics[i] = Statistic();
        statistics[i].label = results.front()[i].label;
        if (results.front()[i].label == "CPUMapping") {
            CPU_idx = i;
        }
    }

    for (TestRun const& run : results) {
        double min_obj = std::numeric_limits<double>::infinity();
        for (TestResult const& res : run) {
            min_obj = std::min(min_obj, res.objective);
        }

        for (size_t i = 0; i < run.size(); ++i) {
            TestResult const& res = run[i];
            Statistic& stat = statistics[i];
            if (res.timeout) {
                ++stat.nbr_timeout;
                continue;
            }

            if (res.objective == min_obj) {
                ++stat.nbr_winner;
            }

            stat.update(res.objective, run[CPU_idx].objective);
            stat.total_time_ms += res.runtime_ms.count();
        }
    }

    return statistics;
}

void results_to_file(std::vector<TestRun> const& results, std::string filename, std::string config_name = "", bool append = false) {
	if (results.empty()) {
		return;
	}

    auto statistics = create_statistics(results);

	std::ofstream ofs("results/" + filename, append ? std::ios_base::app : std::ios_base::out);
	ofs << "Configuration: " << config_name << std::endl;
	for (Statistic const& stat : statistics) {
        if (stat.total_runs > 0) {
		    ofs << std::left << std::setw(25) << stat.label << ";" << std::right << std::setw(10) << stat.total_rel_positive_impr / stat.total_runs << ";" << std::setw(10) << stat.min_impr << ";" << std::setw(10) << stat.max_impr << ";"
			    << std::setw(3) << stat.nbr_impr << ";" << std::setw(10) << stat.total_time_ms / stat.total_runs << ";" << std::setw(3) << stat.nbr_winner << ";" << std::setw(3) << stat.nbr_worsen << ";" << std::setw(3) << stat.nbr_equal << std::endl;
        }
	}
	ofs << std::endl;
}

void create_plot(std::vector<std::pair<int,std::vector<TestRun>>> const& results, std::ostream& out = std::cout) {
	if (results.empty()) return;

    std::unordered_map<std::string, std::vector<std::pair<int,Statistic>>> stat_time_map;
    for (auto const& run_with_size : results) {
        std::vector<Statistic> statistics = create_statistics(run_with_size.second);
        for (Statistic& stat : statistics) {
            stat_time_map[stat.label].push_back({run_with_size.first, stat});
        }
    }

    auto print_plot = [&stat_time_map, &out](std::string const& name, std::function<double(Statistic const&)> measure) {
        out << "\n=== " << name << " ===" << std::endl;
        for (auto& algpair : stat_time_map) {
            out << std::endl;
            out << "\\addlegendentry{" << algpair.first << "}" << std::endl;
            out << "\\addplot coordinates{";
            for (auto& test_run_pair : algpair.second) {
                if (test_run_pair.second.total_runs > 0) {
                    out << "(" << test_run_pair.first << "," << measure(test_run_pair.second) << ") ";
                }
            }
            out << "};" << std::endl;
        }
    };

    print_plot("Execution Time", [](Statistic const& stat){return stat.total_time_ms / (double) stat.total_runs;});
    print_plot("Positive Improvement", [](Statistic const& stat){return stat.total_rel_positive_impr / (double)stat.total_runs;});
    print_plot("RelImpr", [](Statistic const& stat){return stat.total_rel_impr / (double)stat.total_runs;});
    print_plot("MinImpr", [](Statistic const& stat){return stat.min_impr;});
    print_plot("MaxImpr", [](Statistic const& stat){return stat.max_impr;});
    print_plot("NbrImpr", [](Statistic const& stat){return stat.nbr_impr;});
    print_plot("NbrWinner", [](Statistic const& stat){return stat.nbr_winner;});
    print_plot("Timeouts", [](Statistic const& stat){return stat.nbr_timeout;});

    out << "\n=== Total ===" << std::endl;

    for (auto& algpair : stat_time_map) {
        Time total_time = 0;
        double total_rel_posimpr = 0;
        size_t total_runs = 0;
        for (auto& test_run_pair : algpair.second) {
            if (test_run_pair.second.total_runs > 0) {
                total_time += test_run_pair.second.total_time_ms / test_run_pair.second.total_runs;
                total_rel_posimpr += test_run_pair.second.total_rel_positive_impr / test_run_pair.second.total_runs;
                ++total_runs;
            }
        }
        out << std::setw(20) << std::left << algpair.first << " Avg. Impr: " << std::setw(10) << total_rel_posimpr / total_runs << " Time: " << total_time << " ms" << std::endl;
    }
}

void convert_to_table(std::string const& filename) {
	std::ifstream ifs(filename);
	
	std::string line;
	while (std::getline(ifs, line))
	{
		std::stringstream ss(line);

		std::vector<std::string> split_string;
		std::string name;
		while (std::getline(ss, name, ';')) {
			split_string.push_back(name);
		}

		auto get_name = [](std::string const& name) -> std::string {
			std::string name_without_space = name;
			name_without_space.erase(remove_if(name_without_space.begin(), name_without_space.end(), isspace), name_without_space.end());
			if (name_without_space == "DeviceBasedMapping") return "Device-based";
			if (name_without_space == "TimeBasedMapping") return "Time-based";
			if (name_without_space == "TimeBasedMappingStream") return "Time-based Streaming";

			return name_without_space;
		};

		if (split_string.size() >= 6) {
			std::cout << get_name(split_string[0]) << "\t\t& "
				<< "\\SI{" << (long)(std::stod(split_string[1])*100) << "}{\\percent}\t& "
				<< "\\SI{" << (long)(std::stod(split_string[2]) * 100) << "}{\\percent}\t& "
				<< "\\SI{" << (long)(std::stod(split_string[3]) * 100) << "}{\\percent}\t& "
				<< "\\num{" << std::stoi(split_string[4]) << "}\t& "
				<< "\\SI{" << std::fixed << std::setprecision(2) << std::stod(split_string[5]) / 1000.0 << "}{\\second}\t\\\\" << std::endl;
		}

	}
}
