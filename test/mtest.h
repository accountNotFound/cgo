#pragma once

#include <chrono>
#include <exception>
#include <list>
#include <string>

namespace mtest {

class TestException : public std::exception {
 public:
  const char* what() { return "test failed"; }
};

class TestInterface {
 public:
  TestInterface() = default;

  virtual ~TestInterface() = default;

  int run(int i, int total) {
    std::printf(" *** [%d/%d] [%s] test begin\n", i, total, this->_name());
    try {
      auto begin = std::chrono::steady_clock::now();
      this->_run();
      auto end = std::chrono::steady_clock::now();
      float time_cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() / 1000.0;
      std::printf(" *** test pass, time cost %f seconds\n\n", time_cost);
      return 0;
    } catch (...) {
      std::printf(" *** test failed\n\n");
      return -1;
    }
  }

 protected:
  virtual void _run() {}

  virtual const char* _name() const { return ""; }
};

inline static std::list<TestInterface*> g_testers = {};

int main() {
  int res = 0;
  int i = 0;
  for (auto tester : g_testers) {
    if (tester->run(++i, g_testers.size()) != 0) {
      res = -1;
    }
  }
  return res;
}

}  // namespace mtest

#define _STR(x) #x

#define STR(x) _STR(x)

#define TEST_CLASS(test_name, test_suite_name) Test__##test_name##__##test_suite_name

#define TEST(test_name, test_suite_name)                                                                 \
  class TEST_CLASS(test_name, test_suite_name) : public mtest::TestInterface {                           \
   protected:                                                                                            \
    void _run() override;                                                                                \
                                                                                                         \
    const char* _name() const override { return STR(TEST_CLASS(test_name, test_suite_name)); }           \
  };                                                                                                     \
                                                                                                         \
  inline static TEST_CLASS(test_name, test_suite_name)* _g__##test_name##__##test_suite_name##__tester = \
      dynamic_cast<TEST_CLASS(test_name, test_suite_name)*>(                                             \
          mtest::g_testers.emplace_back(new TEST_CLASS(test_name, test_suite_name)()));                  \
                                                                                                         \
  void TEST_CLASS(test_name, test_suite_name)::_run()

#define DISABLE_TEST(test_name, test_suite_name) void disabled_test__##test_name##__##test_suite_name()

#define ASSERT(expr, fmt, ...)                                                                         \
  {                                                                                                    \
    if (!(expr)) {                                                                                     \
      std::printf("%s -> %s() [line: %d] " fmt "\n", __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
      throw mtest::TestException();                                                                    \
    }                                                                                                  \
  }

#define ASSERT_RAISE(expr, ex_type, fmt, ...)      \
  {                                                \
    try {                                          \
      (expr);                                      \
      ASSERT(false, "expect raise " STR(ex_type)); \
    } catch (const ex_type& ex) {                  \
    } catch (...) {                                \
      ASSERT(false, "unexpect raise");             \
    }                                              \
  }

int main() { return mtest::main(); }