#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>

namespace tinyim {

class Logger {
public:
    static void Init();
};

} // namespace tinyim
