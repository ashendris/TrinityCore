#ifndef MOD_GREETING_GREETING_SERVICE_H
#define MOD_GREETING_GREETING_SERVICE_H

#include <string>
#include <string_view>

namespace Trinity::Modules
{
class GreetingService
{
public:
    virtual ~GreetingService() = default;
    virtual std::string Greeting(std::string_view subject) const = 0;
};
}

#endif
