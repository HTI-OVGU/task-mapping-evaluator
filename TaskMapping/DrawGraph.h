#pragma once
#include "System.h"
#include "Mapping.h"
#include "EvaluationLog.h"

void draw_graph(TaskGraph const& task_graph, Mapping const& mapping, std::string const& output_filename, EvaluationLog const& log);
void draw_hardware_graph(Platform const& platform, std::string const& output_filename);