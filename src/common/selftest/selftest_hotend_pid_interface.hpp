#pragma once

#include "i_selftest_part.hpp"
#include "selftest_hotend_pid.hpp"

namespace selftest {

/// Runs the hotend PID tuning described by @p config, creating @p part on the first call
TestReturn phaseHotendPid(IPartHandler *&part, const HotendPidConfig_t &config);

} // namespace selftest
