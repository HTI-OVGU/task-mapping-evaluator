#pragma once

#include "TaskGraph.h"
#include "TaskGraphGenerator.h"
#define BOOST_JSON_NO_LIB
#define BOOST_CONTAINER_NO_LIB
#include <boost/json/src.hpp>
#include <iostream>
#include <fstream>

TaskGraph build_from_json(std::string filename) {
    TaskGraph task_graph;
    TaskPropertyProducer tpprod;

    std::ifstream ifs(filename);
    if (!ifs.good()) {
        std::cerr << "File not found " << filename << std::endl;
        return task_graph;
    }

    std::string input(std::istreambuf_iterator<char>(ifs), {});
    boost::json::value parsed_data = boost::json::parse(input);

    std::unordered_map<std::string, int64_t> speed;
    boost::json::value const& machines = parsed_data.at("workflow").at("machines");
    assert(machines.is_array());
    for (boost::json::value const& machine : machines.get_array()) {
        assert(machine.as_object().if_contains("nodeName"));
        if(machine.as_object().if_contains("cpu") && machine.at("cpu").as_object().if_contains("speed")) {
            assert(machine.at("cpu").at("speed").is_int64());
            speed[machine.at("nodeName").as_string().c_str()] = machine.at("cpu").at("speed").as_int64();
        } else {
            speed[machine.at("nodeName").as_string().c_str()] = 1200; //Default speed
        }
    }

    boost::json::value const& tasks = parsed_data.at("workflow").at("tasks");
    assert(tasks.is_array());

    std::unordered_map<std::string, Task*> task_map;
    for (boost::json::value const& task : tasks.get_array()) {
        auto properties = tpprod.get_properties();

        double const CPUSpeed_MBps = (task.as_object().if_contains("machine") && task.at("machine").is_string()) ? speed.at(task.at("machine").as_string().c_str()) : 0; // MB/s
        double const runtime_s = (task.as_object().if_contains("runtimeInSeconds") && task.at("runtimeInSeconds").is_double()) ? task.at("runtimeInSeconds").as_double() : 0; // s
        double const avgCPU = (task.as_object().if_contains("avgCPU") && task.at("avgCPU").is_double()) ? task.at("avgCPU").as_double() : 0; // Percent
        DataSize output_size_B = 0; // Byte
        DataSize input_size_B = 0; // Byte
        for (boost::json::value const& file : task.at("files").get_array()) {
            if (file.at("link").as_string() == "output") {
                output_size_B += static_cast<DataSize>(file.at("sizeInBytes").as_int64());
            }
            else {
                input_size_B += static_cast<DataSize>(file.at("sizeInBytes").as_int64());
            }
        }
        input_size_B = std::max(input_size_B, (DataSize)1);

        ScaleFactor const complexity = (runtime_s > 0 && avgCPU > 0 && CPUSpeed_MBps > 0) ? runtime_s / ((double)input_size_B / 1024. / 1024. / (CPUSpeed_MBps * avgCPU / 100.)) : 1;
        Task* new_task = task_graph.add_node(complexity, properties.parallelizability, properties.streamability, [output_size_B](std::vector<DataSize> const&) {return std::max(output_size_B / 1024 / 1024, (DataSize)1);});
        //new_task->set_area(20); // Constant area requirement for every task;
        task_map[std::string(task.at("name").get_string())] = new_task;
    }

    // If graph has no children entries, use parents entries instead
    bool use_parents = false;
    if (tasks.get_array().size() > 0 && !tasks.get_array().at(0).as_object().if_contains("children")) {
        use_parents = true;
    }
    for (boost::json::value const& task : tasks.get_array()) {
        Task* curr_task = task_map.at(std::string(task.at("name").get_string()));

        if (use_parents) {
            for (boost::json::value const& parent : task.at("parents").get_array()) {
                task_graph.add_edge(task_map.at(std::string(parent.get_string())), curr_task);
            }
        }
        else {
            for (boost::json::value const& child : task.at("children").get_array()) {
                task_graph.add_edge(curr_task, task_map.at(std::string(child.get_string())));
            }
        }
    }

    /*
    for (Task* task : task_graph.get_tasks()) {
        if (task->get_input_size() == 0) {
            task->set_size_func(&DATA_SRC);
        }
        else if (task->get_output_size() == 0) {
            task->set_size_func(&DATA_SNK);
        }
    }
    */

    return task_graph;
}


size_t size_from_json(std::string filename) {
    std::ifstream ifs(filename);
    if (!ifs.good()) {
        std::cerr << "File not found " << filename << std::endl;
        return -1;
    }

    std::string input(std::istreambuf_iterator<char>(ifs), {});

    boost::json::value parsed_data = boost::json::parse(input);
    boost::json::value const& tasks = parsed_data.at("workflow").at("tasks");
    assert(tasks.is_array());

    return tasks.as_array().size();
}