#pragma once
#include <functional>
#include <string>
namespace pluginhttp {
inline std::function<bool(const std::string&, const std::function<bool(std::string&)>&,
                          const std::function<bool(const std::string&)>&, const std::function<bool()>&)>
    fixture;
inline bool notesRealtime(const std::string& k, const std::function<bool(std::string&)>& o,
                          const std::function<bool(const std::string&)>& i, const std::function<bool()>& c) {
  return fixture(k, o, i, c);
}
}  // namespace pluginhttp
