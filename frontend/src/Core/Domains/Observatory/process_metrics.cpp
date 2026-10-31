#include "process_metrics.h"

#include <limits>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#include <sys/time.h>
#endif

#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#endif

#if defined(Q_OS_LINUX)
#include <QByteArray>
#include <QFile>
#include <QList>
#include <unistd.h>
#endif

namespace ProcessMetrics
{
namespace
{
#if defined(Q_OS_UNIX)
qint64 cpuTime()
{
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
    const auto ns = [](const timeval &time) {
        return static_cast<qint64>(time.tv_sec) * 1000000000LL + static_cast<qint64>(time.tv_usec) * 1000LL;
    };
    return ns(usage.ru_utime) + ns(usage.ru_stime);
}
#endif

#if defined(Q_OS_MACOS)
void readMach(Reading &reading)
{
    mach_task_basic_info_data_t basic{};
    mach_msg_type_number_t basicCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&basic), &basicCount) == KERN_SUCCESS) {
        reading.rssBytes = static_cast<qint64>(basic.resident_size);
    }

    task_vm_info_data_t vm{};
    mach_msg_type_number_t vmCount = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &vmCount) == KERN_SUCCESS) {
        reading.footprintBytes = static_cast<qint64>(vm.phys_footprint);
    }

    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t threadCount = 0;
    if (task_threads(mach_task_self(), &threads, &threadCount) == KERN_SUCCESS) {
        reading.threads = static_cast<int>(threadCount);
        for (mach_msg_type_number_t i = 0; i < threadCount; ++i) {
            mach_port_deallocate(mach_task_self(), threads[i]);
        }
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), threadCount * sizeof(thread_act_t));
    }
}
#endif

#if defined(Q_OS_LINUX)
void readProc(Reading &reading)
{
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> fields = statm.readAll().simplified().split(' ');
        bool ok = false;
        const qint64 pages = fields.size() > 1 ? fields.at(1).toLongLong(&ok) : 0;
        if (ok) {
            reading.rssBytes = pages * static_cast<qint64>(sysconf(_SC_PAGESIZE));
        }
    }
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (stat.open(QIODevice::ReadOnly)) {
        // The command name is in parentheses and may contain spaces.
        const QByteArray text = stat.readAll();
        const qsizetype close = text.lastIndexOf(')');
        if (close > 0) {
            const QList<QByteArray> fields = text.mid(close + 2).simplified().split(' ');
            // After the name: state is field 3, num_threads field 20.
            bool ok = false;
            const int threads = fields.size() > 17 ? fields.at(17).toInt(&ok) : 0;
            if (ok) {
                reading.threads = threads;
            }
        }
    }
}
#endif
} // namespace

Reading read()
{
    Reading reading;
#if defined(Q_OS_UNIX)
    reading.cpuTimeNs = cpuTime();
#endif
#if defined(Q_OS_MACOS)
    readMach(reading);
#elif defined(Q_OS_LINUX)
    readProc(reading);
#endif
    return reading;
}

double cpuPercent(const Reading &earlier, const Reading &later, qint64 wallNs)
{
    if (earlier.cpuTimeNs < 0 || later.cpuTimeNs < 0 || wallNs <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return 100.0 * static_cast<double>(later.cpuTimeNs - earlier.cpuTimeNs) / static_cast<double>(wallNs);
}
} // namespace ProcessMetrics
