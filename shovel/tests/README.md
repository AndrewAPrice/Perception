This directory contains tests and benchmarks for Shovel.

# Benchmarks
Below are a list of the benchmark performances, comparing Shovel's performance against C.

## Fibonacci Recursive

| Date | Shovel (seconds) | Unoptimised C (seconds) | Shovel x | Optimized C (seconds) | Shovel x Optimized C | Comments |
| --- | --- | --- | --- | --- | --- | --- |
| 2014-06-13 | 5.461770 | 0.807574 | 6.76318207372 | 0.030373 | 179.823198235 | Initial interpreter. |
| 2014-06-16 | 5.410971 | 0.803797 | 6.7317631193 | 0.030266 | 178.780512787 | Merged variable and local stacks. | 

## Average

| Date | Shovel x Unoptimized C | Shovel x Optimized C | Comments  |
| --- | --- | --- | --- |
| 2014-06-13 | 6.76318207372 | 179.823198235 | Initial interpreter. |
| 2014-06-16 | 6.7317631193 | 178.780512787 | Merged variable and local stacks. |
