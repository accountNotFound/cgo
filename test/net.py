import sys
import time
import socket
from concurrent.futures import ThreadPoolExecutor

proc_num = 4
cli_num = 10
conn_num = 10

def cli_test():
  def task(cli_id: int):
    for i in range(conn_num):
      sock = socket.socket()
      sock.connect(("0.0.0.0", 8080))
      print(f"{cli_id} {i} connected")
      sock.send(f"cli {cli_id} send data {i}".encode("utf8"))
      print(sock.recv(256).decode("utf8"))
      # time.sleep(2)
    print(f"task {cli_id} end")

  with ThreadPoolExecutor(proc_num) as pool:
    results = pool.map(task, range(cli_num))
  for r in results:
    pass

def svr_test():
  sock = socket.socket()
  sock.bind(("0.0.0.0", 8080))
  sock.listen(64)
  while True:
    c, addr = sock.accept()
    req = c.recv(256).decode("utf8")
    # print(req)
    n = c.send(f"echo from server: '{req}'".encode("utf8"))
    print(f'send {n} bytes, echo from server: "{req}"')
    c.close()


if __name__ == "__main__":
  if sys.argv[1] == "-c":
    cli_test()
  elif sys.argv[1] == "-s":
    svr_test()