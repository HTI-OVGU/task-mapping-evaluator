#pragma once

#include "TaskGraph.h"
#include "Mapping.h"
#include <fstream>

#define KERNEL(...) #__VA_ARGS__
const std::string GENERIC_KERNEL = KERNEL(
	__kernel void KERNEL_NAME(unsigned int N, INPUT_PARAM __global unsigned int* res) {
        {
            const unsigned idx = get_global_id(0);
            VARIABLE_DECLARATION
            unsigned result = 1;
            for (int i = 0; i < PARALLEL_COMPLEXITY; ++i) {
                OPERATIONS
            }
            res[idx] = result;
        }
        if(SERIAL_EXISTS_AND get_global_id(0) == 0) {
            for (int idx = 0; idx < N; ++idx) {
                VARIABLE_DECLARATION
                unsigned result = 1;
                for (int i = 0; i < SERIAL_COMPLEXITY; ++i) {
                    OPERATIONS
                }
                res[idx] = result;
            }
        }
	}
);

std::string generate_kernel(ScaleFactor const& complexity, Percent const& parallelizability, char nbr_inputs = 1) {
	std::string kernel_name = "dummy_" + std::to_string(static_cast<int>(complexity)) + "_" + std::to_string(static_cast<int>(parallelizability)) + "_" + std::to_string(nbr_inputs);
    std::string serial_exists = (parallelizability == 100) ? "false && " : "";

	std::ifstream ifs("export/kernels/" + kernel_name + ".cl");
	if (!ifs) {
		std::ofstream ofs("export/kernels/" + kernel_name + ".cl");

		std::string kernel = GENERIC_KERNEL;
		kernel.replace(kernel.find("KERNEL_NAME"), sizeof("KERNEL_NAME") - 1, kernel_name);
		kernel.replace(kernel.find("SERIAL_EXISTS_AND"), sizeof("SERIAL_EXISTS_AND") - 1, serial_exists);
		kernel.replace(kernel.find("PARALLEL_COMPLEXITY"), sizeof("PARALLEL_COMPLEXITY") - 1, std::to_string(static_cast<int>(complexity*parallelizability)));
		kernel.replace(kernel.find("SERIAL_COMPLEXITY"), sizeof("SERIAL_COMPLEXITY") - 1, std::to_string(static_cast<int>(complexity*(100-parallelizability))));

		char param_name = 'a';
		std::string input_param = "";
		std::string operations = "";
		std::string sum = "";
		std::string declaration = "";
		for (char i = 0; i < nbr_inputs; ++i) {
			input_param += "__global unsigned int const* " + std::string(1, param_name) + ", ";
			declaration += "const unsigned v" + std::string(1, param_name) + " = " + std::string(1, param_name) + "[idx]; ";
			operations += "result = (result + v" + std::string(1, param_name) + ") % 47;";
			++param_name;
		}
		kernel.replace(kernel.find("INPUT_PARAM"), sizeof("INPUT_PARAM") - 1, input_param);
		kernel.replace(kernel.find("VARIABLE_DECLARATION"), sizeof("VARIABLE_DECLARATION") - 1, declaration);
		kernel.replace(kernel.find("VARIABLE_DECLARATION"), sizeof("VARIABLE_DECLARATION") - 1, declaration);
		kernel.replace(kernel.find("OPERATIONS"), sizeof("OPERATIONS") - 1, operations);
		kernel.replace(kernel.find("OPERATIONS"), sizeof("OPERATIONS") - 1, operations);

		ofs << kernel;
	}

	return kernel_name;
}

void export_graph(TaskGraph const& graph, Mapping const& mapping, std::string const& label) {
	std::ofstream ofs("export/" + label + ".graph");
	ofs << "262144" << std::endl; // 1 MB
	for (Task* task : graph.get_tasks()) {
        ofs << task->get_label() << ","
            /* std::ceil to avoid distinct kernels with same name*/
            << generate_kernel(std::ceil(task->get_complexity()) , task->get_parallelizability(), std::max((int)task->get_edges_in().size(),1)) << ","
            << mapping.get_processor(task)->get_label()
            << ",0" /* nbr workitems */;

		for (Edge* edge_out : task->get_edges_out()) {
			ofs << "," << edge_out->get_snk()->get_label();
		}
		ofs << std::endl;
	}
}
