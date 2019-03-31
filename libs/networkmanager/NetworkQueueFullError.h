#ifndef NETWORKQUEUEFULLEXCEPTION_H
#define NETWORKQUEUEFULLEXCEPTION_H

#include "NetworkException.h"

class NetworkQueueFullError : public NetworkException
{
public:
    NetworkQueueFullError(const char *error);
};

#endif
