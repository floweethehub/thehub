# Find Libevent
#
# Once done, this will define:
#
#  Event_FOUND - system has Event
#  Event_INCLUDE_DIRS - the Event include directories
#  Event_LIBRARIES - link these to use Event
#

if (EVENT_INCLUDE_DIR AND EVENT_LIBRARY)
  # Already in cache, be silent
  set(EVENT_FIND_QUIETLY TRUE)
endif (EVENT_INCLUDE_DIR AND EVENT_LIBRARY)

pkg_check_modules(Event REQUIRED libevent_pthreads)

foreach (DIR ${Event_LIBRARY_DIRS})
    LIST(PREPEND Event_LIBRARIES "-L${DIR}")
endforeach ()
