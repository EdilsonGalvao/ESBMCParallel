#!/bin/bash
TM=7200

timeout $TM python crc32_vz.py
timeout $TM python djisktra_vz.py
timeout $TM python Patricia_vz.py
timeout $TM python RC6T_vz.py
timeout $TM python clusteringloop_vz.py
timeout $TM python Fuzzy_vz.py
timeout $TM python Mars_vz.py
