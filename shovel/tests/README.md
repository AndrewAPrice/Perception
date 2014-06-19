This directory contains tests and benchmarks for Shovel.

# Benchmarks
Below are a list of the benchmark performances, comparing Shovel's performance against C.

## Fibonacci Recursive

Unoptimised C Version: 0.807574 seconds
Optimised C Version: 0.030373 seconds

| Date | Shovel (seconds) | Unoptimized C | Optimized C | Comments |
| --- | --- | --- | --- | --- |
| 2014-06-13 | 5.461770 | 6.76318207372 | 179.823198235 | Initial interpreter. |
| 2014-06-16 | 5.410971 | 6.70027885989 | 178.15069305 | Merged variable and local stacks. | 
| 2014-06-19 | 4.426942 | 5.48177875959 | 145.752543377 | Native 64-bit interpreter |

## Average

| Date | Shovel / Unoptimized C | Shovel / Optimized C | Comments  |
| --- | --- | --- | --- |
| 2014-06-13 | 6.76318207372 | 179.823198235 | Initial interpreter. |
| 2014-06-16 | 6.70027885989 | 178.15069305 | Merged variable and local stacks. |
| 2014-06-19 | 5.48177875959 | 145.752543377 | Native 64-bit interpreter |
