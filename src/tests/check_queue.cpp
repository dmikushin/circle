#include <check.h>
#include <stdlib.h>
#include <string>

#include "lanl_circle.hpp"
#include "queue.hpp"

START_TEST(test_queue_init_free) {
  circle::init(nullptr, nullptr);

  circle::internal::Queue q(nullptr);
}
END_TEST

START_TEST(test_queue_pop_empty) {
  std::string result;

  circle::init(nullptr, nullptr);

  circle::internal::Queue q(nullptr);

  q.pop(result);
  fail_if(result.size() > 0, "Something was poped from an empty queue.");
}
END_TEST

START_TEST(test_queue_single_push_pop) {
  const std::string test_string = "Here's a test string!";
  std::string result;

  circle::init(nullptr, nullptr);

  circle::internal::Queue q(nullptr);

  q.push(test_string);
  fail_unless(q.getCount() == 1,
              "Queue count was not correct after a single push.");

  q.pop(result);
  fail_unless(q.getCount() == 0,
              "Queue count was not correct after poping the last element.");

  fail_unless(test_string == result,
              "Result poped from the queue does not match original.");
}
END_TEST

START_TEST(test_queue_multiple_push_pop) {
  std::string result;

  const std::string test_strings[] = {
      "first test string",   "second test string", "third test string",
      "fourth test string",  "fifth test string",  "sixth test string",
      "seventh test string", "eighth test string", "nineth test string",
      "tenth test string"};

  circle::init(nullptr, nullptr);

  circle::internal::Queue q(nullptr);

  /* Warm it up a bit */
  q.push(test_strings[0]);
  q.pop(result);
  q.push(test_strings[1]);
  q.pop(result);

  fail_unless(test_strings[1] == result,
              "The queue pop was not the expected result.");

  fail_unless(q.getCount() == 0,
              "Queue count was not correct after two pushes and two pops.");

  /* Now lets try multiple ones */
  q.push(test_strings[2]);
  q.push(test_strings[3]);
  q.push(test_strings[4]);
  q.push(test_strings[5]);
  q.push(test_strings[6]);
  q.push(test_strings[7]); // count = 6
  q.pop(result);
  q.pop(result);
  q.pop(result);
  q.pop(result); // count = 2
  q.push(test_strings[8]);
  q.push(test_strings[9]);
  q.push(test_strings[0]); // count = 5
  q.pop(result);
  q.pop(result);
  q.pop(result);
  q.pop(result);
  q.pop(result); // count = 0

  fail_unless(test_strings[2] == result,
              "The queue pop was not the expected result.");

  fail_unless(q.getCount() == 0,
              "Queue count was not correct after several operations.");

  /* Lets just try a few randomly */
  q.push(test_strings[1]);
  q.pop(result);
  q.pop(result); // count = 0
  q.push(test_strings[2]);
  q.pop(result);
  q.push(test_strings[3]);
  q.push(test_strings[4]);
  q.push(test_strings[5]); // count = 3
  q.pop(result);           // count = 2

  fail_unless(test_strings[5] == result,
              "The queue pop was not the expected result.");

  fail_unless(q.getCount() == 2,
              "Queue count was not correct after several operations.");
}
END_TEST

Suite *check_queue_suite(void) {
  Suite *s = suite_create("check_queue");
  TCase *tc_core = tcase_create("Core");

  tcase_add_test(tc_core, test_queue_init_free);
  tcase_add_test(tc_core, test_queue_pop_empty);
  tcase_add_test(tc_core, test_queue_single_push_pop);
  tcase_add_test(tc_core, test_queue_multiple_push_pop);

  suite_add_tcase(s, tc_core);

  return s;
}

int main(void) {
  int number_failed;

  Suite *s = check_queue_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

