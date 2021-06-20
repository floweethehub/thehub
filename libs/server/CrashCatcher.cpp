/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2017 Tom Zander <tom@flowee.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if not defined WIN32 and not defined NDEBUG and defined ENABLE_CRASH_CATCHER
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

struct sigaction crashCatcher;

static void cleanup()
{
    sigemptyset(&crashCatcher.sa_mask);
}

static void handleProcessFailure(int sig)
{
    cleanup();
    char app[160];
    const pid_t pid = getpid();
    sprintf(app, "echo 'bt\nthread apply all bt\ndetach' | sudo /usr/bin/gdb --pid %d > thehub-%d.dump", pid, pid);
    system(app);

    fprintf(stderr, "FATAL: %s Fault. Logged StackTrace (%d)\n", (sig == SIGSEGV) ? "Segmentation" : ((sig == SIGBUS) ? "Bus" : "Unknown"), pid);
    abort();
}

void setupBacktraceCatcher()
{
    atexit(cleanup);

    crashCatcher.sa_handler = handleProcessFailure;
    sigemptyset(&crashCatcher.sa_mask);
    crashCatcher.sa_flags = 0;

    sigaddset(&crashCatcher.sa_mask, SIGBUS);
    sigaction(SIGBUS, &crashCatcher, (struct sigaction *)NULL);

    sigaddset(&crashCatcher.sa_mask, SIGSEGV);
    sigaction(SIGSEGV, &crashCatcher, (struct sigaction *)NULL);
}

#else
void setupBacktraceCatcher() { }
#endif
