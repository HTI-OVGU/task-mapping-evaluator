#pragma once

#include "System.h"
#include "Mapping.h"
#include <gurobi_c++.h>

typedef std::unordered_map<Task*, std::unordered_map<Device*, GRBVar> > TaskDeviceMap;

const double TIMEOUT_VAL = 5*60;

void add_scaled_product_to_expr(GRBQuadExpr& expr, Time const& time_factor, GRBQuadExpr const& expr_factor, GRBModel& model);
void setup_task_device_variables(System const& sys, GRBModel& model, TaskDeviceMap& tproc, TaskDeviceMap& tmin, TaskDeviceMap& tmout);
void setup_capacity_constraints(System const& sys, GRBModel& model, TaskDeviceMap& tproc);
void extract_mapping_from_variables(Mapping& mapping, System const& sys, TaskDeviceMap const& tproc, TaskDeviceMap const& tmin, TaskDeviceMap const& tmout);
void print_debug_data(GRBModel& model, std::string const& label, bool print_values = true);