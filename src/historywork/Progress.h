// Copyright 2015 Hcnet Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <cstdint>
#include <string>

namespace hcnet
{

class Application;
struct LedgerRange;

std::string fmtProgress(Application& app, std::string const& task,
                        LedgerRange const& range, uint32_t curr);
}
