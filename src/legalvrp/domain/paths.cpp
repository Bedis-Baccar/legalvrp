#include "legalvrp/domain/paths.hpp"

#include <cstdlib>
#include <string>

namespace legalvrp {

std::filesystem::path repo_root() {
#ifdef _MSC_VER
  char* env = nullptr;
  std::size_t len = 0;
  if (_dupenv_s(&env, &len, "LEGALVRP_ROOT") == 0 && env != nullptr) {
    std::filesystem::path p{env};
    std::free(env);
    return p;
  }
#else
  if (const char* env = std::getenv("LEGALVRP_ROOT")) {
    return std::filesystem::path{env};
  }
#endif
  return std::filesystem::path{LEGALVRP_SOURCE_ROOT};
}

}  // namespace legalvrp
