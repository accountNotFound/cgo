for i in {1..50}; do
  echo ${i}-th test:
  ctest -C test
done