import re
import os

ctest_pattern = r'Test #\d+: (?P<test_case>[0-9a-zA-Z_-]+).*?Passed\s+(?P<time_cost>\d+\.\d+)'

def ctest_stat(filename:str):
  tests :dict[str, list] = {}
  with open(filename, 'r', encoding='utf8') as fin:
    for line in fin.readlines():
      match = re.search(ctest_pattern, line)
      if not match:
        continue
      if match.group('test_case') not in tests:
        tests[match.group('test_case')]=[]
      tests[match.group('test_case')].append(float(match.group('time_cost')))

  print(filename)
  for case_name, time_costs in tests.items():
    print(case_name, f'{sum(time_costs)/len(time_costs):.4} sec')
  print()

if 1:
  for f in os.listdir('./log'):
    ctest_stat(f'./log/{f}')