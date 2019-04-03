#include "UTXOInteralError.h"

UTXOInternalError::UTXOInternalError(const char *error)
    : std::runtime_error(error)
{
}
