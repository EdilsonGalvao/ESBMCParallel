#!/bin/bash
TM=7200

timeout $TM python ../Benchmarks/vZ/crc32_vz.py
timeout $TM python ../Benchmarks/vZ/djisktra_vz.py
timeout $TM python ../Benchmarks/vZ/Patricia_vz.py
timeout $TM python ../Benchmarks/vZ/RC6T_vz.py
timeout $TM python ../Benchmarks/vZ/clusteringloop_vz.py
timeout $TM python ../Benchmarks/vZ/Fuzzy_vz.py
timeout $TM python ../Benchmarks/vZ/Mars_vz.py
