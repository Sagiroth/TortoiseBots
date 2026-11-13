#pragma once

#include <string>

namespace ai
{
// csv is ",A,B," (leading and trailing comma). True only for an exact name entry.
inline bool SpellListHasName(std::string const& csv, std::string const& name)
{ return !name.empty() && csv.find("," + name + ",") != std::string::npos; }
}
