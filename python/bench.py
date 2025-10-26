#!/usr/bin/env python3

import numpy as np
import time
import psutil
import asyncio
import aiofiles
import subprocess
from typing import Dict, List, Tuple
import logging

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    filename='benchmark.log'
)

class BenchmarkResults:
    def __init__(self):
        self.execution_time: float = 0
        self.cpu_utilization: List[float] = []
        self.disk_latency: List[float] = []
        
    def to_dict(self) -> Dict:
        return {
            'execution_time': self.execution_time,
            'avg_cpu_utilization': np.mean(self.cpu_utilization) if self.cpu_utilization else 0,
            'avg_disk_latency': np.mean(self.disk_latency) if self.disk_latency else 0
        }

class CPUBenchmark:
    def __init__(self, matrix_size: int = 1000):
        self.matrix_size = matrix_size
        
    def run_matrix_operations(self) -> BenchmarkResults:
        results = BenchmarkResults()
        
        # Create large matrices for operations
        matrix_a = np.random.rand(self.matrix_size, self.matrix_size)
        matrix_b = np.random.rand(self.matrix_size, self.matrix_size)
        
        start_time = time.time()
        
        # Perform intensive floating-point operations
        operations = [
            lambda: np.dot(matrix_a, matrix_b),
            lambda: np.linalg.svd(matrix_a),
            lambda: np.fft.fft2(matrix_a),
            lambda: np.exp(matrix_a)
        ]
        
        for operation in operations:
            # Record CPU utilization during operation
            cpu_start = psutil.cpu_percent()
            operation()
            results.cpu_utilization.append(psutil.cpu_percent())
            
        results.execution_time = time.time() - start_time
        logging.info(f"CPU Benchmark completed in {results.execution_time:.2f} seconds")
        return results

class DiskBenchmark:
    def __init__(self, file_size_mb: int = 100):
        self.file_size_mb = file_size_mb
        self.test_file = "benchmark_test_file.dat"
        
    async def run_io_operations(self) -> BenchmarkResults:
        results = BenchmarkResults()
        start_time = time.time()
        
        # Generate random data
        data = np.random.bytes(self.file_size_mb * 1024 * 1024)
        
        # Write test
        async with aiofiles.open(self.test_file, mode='wb') as f:
            write_start = time.time()
            await f.write(data)
            results.disk_latency.append(time.time() - write_start)
            
        # Read test
        async with aiofiles.open(self.test_file, mode='rb') as f:
            read_start = time.time()
            await f.read()
            results.disk_latency.append(time.time() - read_start)
            
        results.execution_time = time.time() - start_time
        
        # Clean up
        subprocess.run(['rm', self.test_file])
        logging.info(f"Disk Benchmark completed in {results.execution_time:.2f} seconds")
        return results

async def main():
    # Run CPU benchmark
    cpu_bench = CPUBenchmark(matrix_size=1000)
    cpu_results = cpu_bench.run_matrix_operations()
    
    # Run Disk benchmark
    disk_bench = DiskBenchmark(file_size_mb=100)
    disk_results = await disk_bench.run_io_operations()
    
    # Print results
    print("\nBenchmark Results:")
    print("-" * 50)
    print("CPU Benchmark:")
    print(f"Execution Time: {cpu_results.execution_time:.2f} seconds")
    print(f"Average CPU Utilization: {np.mean(cpu_results.cpu_utilization):.2f}%")
    
    print("\nDisk Benchmark:")
    print(f"Execution Time: {disk_results.execution_time:.2f} seconds")
    print(f"Average Disk Latency: {np.mean(disk_results.disk_latency):.4f} seconds")

if __name__ == "__main__":
    for i in range(100):
        asyncio.run(main())
