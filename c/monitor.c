#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>

// Structure to hold monitoring data
typedef struct {
    double execution_time; // seconds
    double cpu_usage;      // percent
    double disk_latency;   // milliseconds
} MonitoringData;

// Read CPU usage snapshot from /proc/stat and return active and total ticks
static int read_cpu_snapshot(uint64_t *active, uint64_t *total) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    uint64_t user=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0, steal=0;
    // read first line starting with "cpu"
    if (fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 4) {
        fclose(f);
        return -1;
    }
    fclose(f);

    *active = user + nice + system + irq + softirq + steal;
    *total = *active + idle + iowait;
    return 0;
}

// Compute CPU usage percentage over the interval between two snapshots
static double compute_cpu_usage_between() {
    uint64_t a1=0, t1=0, a2=0, t2=0;
    if (read_cpu_snapshot(&a1, &t1) != 0) return -1.0;
    // small sleep to get a measurable delta
    struct timespec ts = {0, 20000000}; // 20ms
    nanosleep(&ts, NULL);
    if (read_cpu_snapshot(&a2, &t2) != 0) return -1.0;

    uint64_t active_delta = (a2 > a1) ? (a2 - a1) : 0;
    uint64_t total_delta = (t2 > t1) ? (t2 - t1) : 0;
    if (total_delta == 0) return 0.0;
    return (double)active_delta / (double)total_delta * 100.0;
}

// Function to measure disk latency (write + fsync) in milliseconds
double measure_disk_latency(void) {
    struct timespec start, end;
    char temp_file[] = "temp_latency_test_XXXXXX";
    int fd = mkstemp(temp_file);
    if (fd == -1) {
        fprintf(stderr, "mkstemp failed: %s\n", strerror(errno));
        return -1.0;
    }

    const char *payload = "ping";
    ssize_t wret;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
    }

    wret = write(fd, payload, strlen(payload));
    if (wret == -1) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
    }

    if (fsync(fd) != 0) {
        fprintf(stderr, "fsync failed: %s\n", strerror(errno));
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        perror("clock_gettime");
    }

    close(fd);
    unlink(temp_file);

    double latency_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;
    return latency_ms;
}

// Function to perform monitoring: times the target function and gathers metrics
MonitoringData monitor_performance(void (*target_function)(void)) {
    MonitoringData data = {0};
    struct timespec s, e;

    // start time
    if (clock_gettime(CLOCK_MONOTONIC, &s) != 0) perror("clock_gettime");

    // measure CPU usage during a small window around the workload
    double cpu_before = compute_cpu_usage_between();

    // execute workload
    target_function();

    // end time
    if (clock_gettime(CLOCK_MONOTONIC, &e) != 0) perror("clock_gettime");

    data.execution_time = (e.tv_sec - s.tv_sec) + (e.tv_nsec - s.tv_nsec) / 1e9;

    // a second CPU sample to approximate usage during workload
    double cpu_after = compute_cpu_usage_between();
    if (cpu_before < 0 && cpu_after < 0) data.cpu_usage = -1.0;
    else if (cpu_before < 0) data.cpu_usage = cpu_after;
    else if (cpu_after < 0) data.cpu_usage = cpu_before;
    else data.cpu_usage = (cpu_before + cpu_after) / 2.0;

    data.disk_latency = measure_disk_latency();

    return data;
}

// Write monitoring results to a log file (append)
int write_results_to_file(const char *path, MonitoringData *d) {
    FILE *f = fopen(path, "a");
    if (!f) {
        fprintf(stderr, "Failed to open %s for append: %s\n", path, strerror(errno));
        return -1;
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] Execution Time: %.6f s, CPU: %.2f%%, Disk Latency: %.3f ms\n",
            tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
            d->execution_time, d->cpu_usage, d->disk_latency);

    fflush(f);
    // try to fsync the file descriptor to ensure it's written to disk
    int fd = fileno(f);
    if (fd != -1) fsync(fd);
    fclose(f);
    return 0;
}

// Example workload that writes to a file and simulates CPU work
void sample_workload(void) {
    // CPU work
    for (int i = 0; i < 1000000; ++i) {
        volatile double x = rand() / (double)RAND_MAX;
        (void)x;
    }

    // I/O work: write test.txt and check for errors
    const char *fname = "test.txt";
    FILE *f = fopen(fname, "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing: %s\n", fname, strerror(errno));
        return;
    }
    if (fprintf(f, "test data\n") < 0) {
        fprintf(stderr, "Failed to write to %s\n", fname);
    }
    fflush(f);
    int fd = fileno(f);
    if (fd != -1) {
        if (fsync(fd) != 0) fprintf(stderr, "fsync(%s) failed: %s\n", fname, strerror(errno));
    }
    fclose(f);
}

int main(void) {
    printf("Starting performance monitoring...\n");

    MonitoringData results = monitor_performance(sample_workload);

    printf("\nPerformance Monitoring Results:\n");
    printf("--------------------------------\n");
    printf("Execution Time: %.6f seconds\n", results.execution_time);
    if (results.cpu_usage >= 0) printf("CPU Usage: %.2f%%\n", results.cpu_usage);
    else printf("CPU Usage: (unavailable)\n");
    if (results.disk_latency >= 0) printf("Disk Latency: %.3f ms\n", results.disk_latency);
    else printf("Disk Latency: (unavailable)\n");

    // Also write results to a log file for post-run analysis
    const char *log_path = "monitor_results.log";
    if (write_results_to_file(log_path, &results) != 0) {
        fprintf(stderr, "Failed to write monitoring results to %s\n", log_path);
        return 2;
    }

    printf("Results appended to %s\n", log_path);
    return 0;
}
