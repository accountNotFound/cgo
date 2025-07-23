#pragma once

#include <chrono>
#include <exception>
#include <memory>
#include <string>

namespace mtest {

class TestException : public std::exception {
 public:
  const char* what() { return "test failed"; }
};

class BaseTest {
 public:
  virtual void run() {}

  virtual auto name() const -> const char* { return ""; }
};

class Context {
 public:
  static int run() {
    for (size_t i = 0; i < Context::_testers.size(); ++i) {
      auto& tester = *Context::_testers[i];
      std::printf(" *** [%lu/%lu] [%s] test begin\n", i, Context::_testers.size(), tester.name());
      try {
        auto begin = std::chrono::steady_clock::now();
        tester.run();
        auto end = std::chrono::steady_clock::now();
        float time_cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() / 1000.0;
        std::printf(" *** test pass, time cost %f seconds\n\n", time_cost);
      } catch (...) {
        std::printf(" *** test failed\n\n");
        return -1;
      }
    }
    return 0;
  }

  template <typename T, typename... Args>
    requires std::is_base_of_v<BaseTest, T>
  constexpr static void regist(Args&&... args) {
    Context::_testers.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
  }

 protected:
  inline static std::vector<std::unique_ptr<BaseTest>> _testers = {};
};

}  // namespace mtest

#define _STR(x) #x

#define STR(x) _STR(x)

#define TEST_CLASS(test_name, test_suite_name) Test__##test_name##__##test_suite_name

#define TEST(test_name, test_suite_name)                                                                \
  class TEST_CLASS(test_name, test_suite_name) {                                                        \
   private:                                                                                             \
    class Impl : public mtest::BaseTest {                                                               \
     public:                                                                                            \
      auto name() const -> const char* override { return STR(TEST_CLASS(test_name, test_suite_name)); } \
                                                                                                        \
      void run() override;                                                                              \
    };                                                                                                  \
                                                                                                        \
    inline static auto _dumpy = (mtest::Context::regist<Impl>(), 0);                                    \
  };                                                                                                    \
                                                                                                        \
  void TEST_CLASS(test_name, test_suite_name)::Impl::run()

#define DISABLE_TEST(test_name, test_suite_name) void disabled_test__##test_name##__##test_suite_name()

#define ASSERT(expr, fmt, ...)                                                                               \
  {                                                                                                          \
    if (!(expr)) {                                                                                           \
      std::printf("%s -> %s() [line: %d] ASSERT(" STR(expr) ") " fmt "\n", __FILE__, __FUNCTION__, __LINE__, \
                  ##__VA_ARGS__);                                                                            \
      throw mtest::TestException();                                                                          \
    }                                                                                                        \
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

int main() { return mtest::Context::run(); }