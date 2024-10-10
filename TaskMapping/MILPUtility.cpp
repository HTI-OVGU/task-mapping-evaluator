#include "MILPUtility.h"
#include <fstream>

void add_scaled_product_to_expr(GRBQuadExpr& expr, Time const& time_factor, GRBQuadExpr const& expr_factor, GRBModel& model) {
    if (time_factor != 0) {
        if (time_factor == std::numeric_limits<Time>::infinity()) {
            model.addQConstr(expr_factor == 0);
        }
        else {
            expr += time_factor/1000 * expr_factor;
        }
    }
}

void setup_task_device_variables(System const& sys, GRBModel& model, TaskDeviceMap& tproc, TaskDeviceMap& tmin, TaskDeviceMap& tmout) {
    for (Task* task : sys.get_task_graph().get_tasks()) {
        GRBLinExpr proc_expr = 0;
        GRBLinExpr min_expr = 0;
        GRBLinExpr mout_expr = 0;

        std::string const task_label = task->get_label();
        for (Processor* proc : sys.get_platform().get_processors()) {
            tproc[task][proc] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, "P_" + task_label + "_" + proc->get_label());

            if (!sys.is_compatible(task, proc)) {
                model.addConstr(0 == tproc[task][proc]);
            }
            else {
                proc_expr += tproc[task][proc];
            }
        }

        for (Memory* mem : sys.get_platform().get_memories()) {
            tmin[task][mem] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, "I_" + task_label + "_" + mem->get_label());
            tmout[task][mem] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, "O_" + task_label + "_" + mem->get_label());

            if (!sys.is_compatible(task, mem)) {
                model.addConstr(0 == tmin[task][mem]);
                model.addConstr(0 == tmout[task][mem]);
            }
            else {
                min_expr += tmin[task][mem];
                mout_expr += tmout[task][mem];
            }
        }

        model.addConstr(1 == proc_expr);
        model.addConstr(1 == min_expr);
        model.addConstr(1 == mout_expr);
    }
}

void setup_capacity_constraints(System const& sys, GRBModel& model, TaskDeviceMap& tproc) {
    for (Processor* proc : sys.get_platform().get_processors()) {
        if (proc->has_maximum_capacity()) {
            GRBLinExpr expr = 0;
            for (Task* task : sys.get_task_graph().get_tasks()) {
                expr += task->get_area_requirement() * tproc[task][proc];
            }
            model.addConstr(proc->get_maximum_capacity() >= expr, "CAPACITY_" + proc->get_label());
        }
    }
}

void extract_mapping_from_variables(Mapping& mapping, System const& sys, TaskDeviceMap const& tproc, TaskDeviceMap const& tmin, TaskDeviceMap const& tmout) {
    for (Task* task : sys.get_task_graph().get_tasks()) {
        Processor* mapped_proc = nullptr;
        Memory* mapped_mem_in = nullptr;
        Memory* mapped_mem_out = nullptr;

        for (Processor* proc : sys.get_platform().get_processors()) {
            if (tproc.at(task).at(proc).get(GRB_DoubleAttr_X) > 0.9) {
                mapped_proc = proc;
                break;
            }
        }
        for (Memory* mem : sys.get_platform().get_memories()) {
            if (tmin.at(task).at(mem).get(GRB_DoubleAttr_X) > 0.9) {
                mapped_mem_in = mem;
            }
            if (tmout.at(task).at(mem).get(GRB_DoubleAttr_X) > 0.9) {
                mapped_mem_out = mem;
            }
        }
        mapping.map(task, mapped_proc, mapped_mem_in, mapped_mem_out);
    }
}

void print_debug_data(GRBModel& model, std::string const& label, bool print_values) {
    model.update();
    model.write("results/"+ label);

    if (print_values) {
        std::ofstream ofs;
        ofs.open("results/" + label, std::ios_base::app);

        if (model.get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
            GRBVar* vars = NULL;
            vars = model.getVars();
            // Print the values of all variables
            ofs << std::endl << "Variable values:" << std::endl;
            for (int i = 0; i < model.get(GRB_IntAttr_NumVars); i++) {
                ofs << vars[i].get(GRB_StringAttr_VarName) << " = " << vars[i].get(GRB_DoubleAttr_X) << std::endl;
            }
        }
        else {
            ofs << "Model infeasible!" << std::endl;
        }
    }
}