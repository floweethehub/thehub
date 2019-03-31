#include "NetworkQueueFullError.h"

NetworkQueueFullError::NetworkQueueFullError(const char *error)
    : NetworkException(error)
{
}
