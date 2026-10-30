#pragma once

#include <QtGlobal>

// This process's own CPU time, memory and thread count, read from the
// operating system (getrusage, Mach task_info on macOS, /proc on Linux).
// A figure the platform doesn't provide is -1, never an estimate.
namespace ProcessMetrics
{
struct Reading
{
    qint64 cpuTimeNs = -1;      // user + system CPU time so far
    qint64 rssBytes = -1;       // resident memory
    qint64 footprintBytes = -1; // macOS phys_footprint, as Activity Monitor shows
    int threads = -1;
};

Reading read();

// CPU use between two readings taken wallNs apart, as % of one core
// (200 = two cores busy). NaN when either reading has no CPU time.
double cpuPercent(const Reading &earlier, const Reading &later, qint64 wallNs);
} // namespace ProcessMetrics
