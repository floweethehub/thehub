#include "NetworkException.h"

NetworkException::NetworkException(const char *error)
    : std::runtime_error(error)
{
}
