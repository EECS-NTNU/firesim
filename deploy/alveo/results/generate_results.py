import sys
import shutil
from os import listdir

configs = ["baseline", "nda", "stt-register", "stt-rename"]
sum_file = "all_results.csv"

file_headers = []

all_results = {}
for config in configs:
    files = listdir(config)
    files = [x for x in files if "inst-latency" not in x]
    assert(len(files) > 0)

    results = {}
    for file in files:
        benchmark_name = file.split("_")[0]
        benchmark_results = {}
        with open(f"{config}/{file}", "r") as result:
            headers = result.readline().split(";")
            if (len(file_headers) == 0):
                file_headers = ["config-benchmark"] + headers
            numbers = result.readline().split(";")
            assert(len(headers) == len(numbers))
            for x in range(len(headers)):
                benchmark_results[headers[x]] = numbers[x]
        results[benchmark_name] = benchmark_results
    all_results[config] = results

with open(sum_file, "w") as wfile:
    wfile.write(";".join(file_headers))
    for key in all_results:
        sort_dict = dict(sorted(all_results[key].items()))
        for benchmark in sort_dict:
            line = [key + "-" + benchmark]
            for header in sort_dict[benchmark]:
                line += [sort_dict[benchmark][header]]
            wfile.write(";".join(line))


