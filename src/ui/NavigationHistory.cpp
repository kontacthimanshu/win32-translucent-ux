#include <te/ui/NavigationHistory.h>

#include <te/shell/ShellTypes.h>

namespace te
{

// The application's history (T054): compiled once here, compared with
// ShellLocation::operator== (ILIsEqual). The tests use the same template with a stand-in.
template class NavigationHistory<ShellLocation>;

} // namespace te
