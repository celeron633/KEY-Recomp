// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "guest_abi/cpu_context.h"
#include <string>
namespace key {
std::string format_report(const CpuContext& ctx);
void report(const CpuContext& ctx);
void set_report_path(std::string path);
}
