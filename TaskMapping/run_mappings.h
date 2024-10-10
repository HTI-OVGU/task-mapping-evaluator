#pragma once

#include "GreedyMapper.h"
#include "PathBasedMapper.h"
#include "SeriesParallelDecompositionMapper.h"
#include "SingleNodeDecompositionMapper.h"
#include "SimulatedAnnealingMapper.h"
#include "NSGAIIMapper.h"
#include "HEFTMapper.h"
#include "PEFTMapper.h"
#include "ZhouLiuMILPMapper.h"
#include "DeviceBasedMILPMapper.h"
#include "TimeBasedMILPMapper.h"

#include "Evaluation.h"
#include "DrawGraph.h"
#include "GraphExport.h"

#include "ResultHandling.h"

#include <string>
#include <chrono>
#include <unordered_set>

enum class MappingType {
    CPU, GPU, FPGA,
    SingleNode, SNThreshold, SNFirstFit,
    SeriesParallel, SPThreshold, SPFirstFit,
    DeviceMILP, TimeMILP, TimeMILPStream,
	SimulatedAnnealing,
	NSGAII, NSGAIISimple,
    ZhouLiu,
    HEFT, PEFT
};

void run_mapping(std::string const& label, System const& system, Mapper const& mapper, TestRun& test_run, bool draw = true, bool enable_export = false) {

	std::cout << "Computing " << label << "...";

	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
	Mapping mapping = mapper.get_task_mapping(system);
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

	std::cout << " finished!" << std::endl;

    if (mapping.empty()) {
        test_run.push_back({ label, std::numeric_limits<Time>::infinity(), std::chrono::milliseconds::max(), true });
        return;
    }

	MappingEvaluator eval(system, true);
	Time result = eval.evaluate_mapping_with_check(mapping, 100);

	if (result == -1) {
		std::cerr << "No mapping found for " << label << std::endl;
		return;
	}

	if (draw) draw_graph(system.get_task_graph(), mapping, label, eval.get_log());
	if (enable_export) export_graph(system.get_task_graph(), mapping, label);
	test_run.push_back({ label, result, std::chrono::duration_cast<std::chrono::milliseconds>(end - begin), false });
}

void run_mapping_with_schedule(std::string const& label, System const& system, TaskMapperWithSchedule const& mapper, TestRun& test_run, bool draw = true, bool enable_export = false) {
	std::cout << "Computing " << label << "...";

	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
	Mapping mapping = mapper.get_task_mapping(system);
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

	std::cout << " finished!" << std::endl;

	MappingEvaluator eval(system, true);	
	
	Task* dbg_task;
	if (!eval.is_complete(mapping, &dbg_task)) {
		std::cerr << "Mapping incomplete. Missing value for task " << dbg_task->get_label() << std::endl;
		return;
	}
	if (!eval.is_compatible(mapping, &dbg_task)) {
		std::cerr << "Mapping invalid. Incompatible processor for task " << dbg_task->get_label() << std::endl;
		return;
	}
	Processor const* dbg_processor;
	if (!eval.satisfies_capacity_constraint(mapping, &dbg_processor)) {
		std::cerr << "Mapping invalid. Not enough capacity for " << dbg_processor->get_label() << std::endl;
		return;
	}

	std::vector<Task*> sorted_tasks;
	auto task_schedule = mapper.get_task_schedule();
	while (!task_schedule.empty()) {
		sorted_tasks.push_back(task_schedule.top().second);
		task_schedule.pop();
	}

	SortingWrapper heft_sorting(sorted_tasks);
	Time result = eval.compute_cost_with_sorting(mapping, heft_sorting);

	if (result == -1) {
		std::cerr << "No mapping found for " << label << std::endl;
		return;
	}

	if (draw) draw_graph(system.get_task_graph(), mapping, label, eval.get_log());
	if (enable_export) export_graph(system.get_task_graph(), mapping, label);
	test_run.push_back({ label, result, std::chrono::duration_cast<std::chrono::milliseconds>(end - begin) });
}

void run_nsgaii_mapping(System const& system, TestRun& test_run, size_t generations) {
	run_mapping("NSGAIIMapping", system, NSGAIIMapper(generations), test_run, false);
}

void run_mappings(System const& system, TestRun& test_run, std::vector<MappingType> selection, bool draw_results, bool enable_export = false) {
	struct BasePolicies {
		typedef EvaluateAll EvaluationPolicy;
		typedef GreedyBase BaseMappingPolicy;
	};

	struct ThresholdPolicy {
		typedef EvaluateThreshold<15> EvaluationPolicy;
		typedef GreedyBase BaseMappingPolicy;
	};

	struct FirstFitPolicy {
		typedef EvaluateThreshold<10> EvaluationPolicy;
		typedef GreedyBase BaseMappingPolicy;
	};

	struct TwoStagePolicies {
		typedef EvaluateAll EvaluationPolicy;
		typedef SPDBase<EvaluationPolicy> BaseMappingPolicy;
	};

    auto run_func = [&](std::string const& label, Mapper const& mapper) {
        run_mapping(label, system, mapper, test_run, draw_results, enable_export);
    };

    for (MappingType const& mptype : selection) {
        switch(mptype) {
            case MappingType::CPU:
                run_func("CPUMapping", GreedyMapper({ "CPU", "Main_RAM" }));
                break;
            case MappingType::GPU:
                run_func("OnlyGPUMapping", GreedyMapper({ "GPU", "GPU_RAM", "CPU", "Main_RAM" }));
                break;
            case MappingType::FPGA:
                run_func("OnlyFPGAMapping", GreedyMapper({ "FPGA", "FPGA_RAM", "CPU", "Main_RAM" }));
                break;
            case MappingType::SeriesParallel:
                run_func("SeriesParallelMapping", SeriesParallelDecompositionMapper<BasePolicies>());
                break;
            case MappingType::SingleNode:
                run_func("SingleNodeMapping", SingleNodeDecompositionMapper<BasePolicies>());
                break;
            case MappingType::SPThreshold:
                run_func("SPThresholdMapping", SeriesParallelDecompositionMapper<ThresholdPolicy>());
                break;
            case MappingType::SNThreshold:
                run_func("SNThresholdMapping", SingleNodeDecompositionMapper<ThresholdPolicy>());
                break;
            case MappingType::SPFirstFit:
                run_func("SPFirstFitMapping", SeriesParallelDecompositionMapper<FirstFitPolicy>());
                break;
            case MappingType::SNFirstFit:
                run_func("SNFirstFitMapping", SingleNodeDecompositionMapper<FirstFitPolicy>());
                break;
			case MappingType::SimulatedAnnealing:
				run_func("SimulatedAnnealingMapping", SimulatedAnnealingMapper());
				break;
			case MappingType::NSGAII:
				run_func("NSGAIIMapping", NSGAIIMapper());
				break;
			case MappingType::NSGAIISimple:
				run_func("NSGAIIMappingSummed", NSGAIIMapper<SummedEvaluation>());
				break;
            case MappingType::HEFT:
                run_func("HEFTMapping", HEFTMapper());
                break;
            case MappingType::PEFT:
                run_func("PEFTMapping", PEFTMapper());
                break;
            case MappingType::ZhouLiu:
                run_func("ZhouLiuMapping", ZhouLiuMILPMapper());
                break;
            case MappingType::DeviceMILP:
                run_func("DeviceBasedMapping", DeviceBasedMILPMapper());
                break;
            case MappingType::TimeMILP:
                run_func("TimeBasedMapping", TimeBasedMILPMapper());
                break;
            case MappingType::TimeMILPStream:
                run_func("TimeBasedMappingStream", TimeBasedMILPMapper(true));
                break;
        }
    }

	//run_mapping("PathBasedMapping", system, PathBasedMapper(), test_run, draw_results, enable_export);
   //run_mapping("TwoPhaseMapping", system, SingleNodeDecompositionMapper<TwoStagePolicies>(), test_run, draw_results, enable_export);
	//run_mapping_with_schedule("HEFTMappingSchedule", system, HEFTMapper(), test_run, draw_results, enable_export);
	//run_mapping_with_schedule("PEFTMappingSchedule", system, PEFTMapper(), test_run, draw_results, enable_export);
}

void run_default_mappings(System const& system, TestRun& test_run, bool draw_results, bool enable_export = false) {
    run_mappings(system, test_run, {MappingType::CPU, MappingType::SeriesParallel, MappingType::SPFirstFit, MappingType::SingleNode, MappingType::SNFirstFit, MappingType::SimulatedAnnealing, MappingType::NSGAII, MappingType::HEFT, MappingType::PEFT, MappingType::DeviceMILP}, draw_results, enable_export);
}
