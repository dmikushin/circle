#include <check.h>
#include <stdlib.h>
#include <string>

#include "lanl_circle.hpp"
#include "queue.hpp"

START_TEST(test_queue_init_free) {
  int free_result = -1;

  ::circle::internal::Queue *q;
  circle::init(0, NULL, circle::RuntimeFlags::DefaultFlags);

  q = ::circle::internal::Queue::init();
  fail_if(q == NULL, "Initializing a queue failed.");

  free_result = ::circle::internal::Queue::free(q);
  fail_unless(free_result >= 0, "Queue was not null after free.");

  circle::finalize();
}
END_TEST

START_TEST(test_queue_pop_empty) {
  int free_result = -1;
  std::string result;

  ::circle::internal::Queue *q;
  circle::init(0, NULL, circle::RuntimeFlags::DefaultFlags);

  q = ::circle::internal::Queue::init();
  fail_if(q == NULL, "Initializing a queue failed.");

  ::circle::internal::Queue::pop(q, result);
  fail_if(result.size() > 0, "Something was poped from an empty queue.");

  free_result = ::circle::internal::Queue::free(q);
  fail_unless(free_result, "Circle context was not null after free.");

  circle::finalize();
}
END_TEST

START_TEST(test_queue_single_push_pop) {
  int free_result = -1;
  const std::string test_string = "Here's a test string!";
  std::string result;

  ::circle::internal::Queue *q;
  circle::init(0, NULL, circle::RuntimeFlags::DefaultFlags);

  q = ::circle::internal::Queue::init();
  fail_if(q == NULL, "Initializing a queue failed.");

  ::circle::internal::Queue::push(q, test_string);
  fail_unless(q->count == 1,
              "Queue count was not correct after a single push.");

  ::circle::internal::Queue::pop(q, result);
  fail_unless(q->count == 0,
              "Queue count was not correct after poping the last element.");

  fail_unless(test_string == result,
              "Result poped from the queue does not match original.");

  free_result = ::circle::internal::Queue::free(q);
  fail_unless(free_result, "Circle context was not null after free.");

  circle::finalize();
}
END_TEST

START_TEST(test_queue_multiple_push_pop) {
  int free_result = -1;
  std::string result;

  const std::string test_strings[] = {
      "first test string",   "second test string", "third test string",
      "fourth test string",  "fifth test string",  "sixth test string",
      "seventh test string", "eighth test string", "nineth test string",
      "tenth test string"};

  ::circle::internal::Queue *q;
  circle::init(0, NULL, circle::RuntimeFlags::DefaultFlags);

  q = ::circle::internal::Queue::init();
  fail_unless(q != NULL, "Initializing a queue failed.");

  /* Warm it up a bit */
  ::circle::internal::Queue::push(q, test_strings[0]);
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::push(q, test_strings[1]);
  ::circle::internal::Queue::pop(q, result);

  fail_unless(test_strings[1] == result,
              "The queue pop was not the expected result.");

  fail_unless(q->count == 0,
              "Queue count was not correct after two pushes and two pops.");

  /* Now lets try multiple ones */
  ::circle::internal::Queue::push(q, test_strings[2]);
  ::circle::internal::Queue::push(q, test_strings[3]);
  ::circle::internal::Queue::push(q, test_strings[4]);
  ::circle::internal::Queue::push(q, test_strings[5]);
  ::circle::internal::Queue::push(q, test_strings[6]);
  ::circle::internal::Queue::push(q, test_strings[7]); // count = 6
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::pop(q, result); // count = 2
  ::circle::internal::Queue::push(q, test_strings[8]);
  ::circle::internal::Queue::push(q, test_strings[9]);
  ::circle::internal::Queue::push(q, test_strings[0]); // count = 5
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::pop(q, result); // count = 0

  fail_unless(test_strings[2] == result,
              "The queue pop was not the expected result.");

  fail_unless(q->count == 0,
              "Queue count was not correct after several operations.");

  /* Lets just try a few randomly */
  ::circle::internal::Queue::push(q, test_strings[1]);
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::pop(q, result); // count = 0
  ::circle::internal::Queue::push(q, test_strings[2]);
  ::circle::internal::Queue::pop(q, result);
  ::circle::internal::Queue::push(q, test_strings[3]);
  ::circle::internal::Queue::push(q, test_strings[4]);
  ::circle::internal::Queue::push(q, test_strings[5]); // count = 3
  ::circle::internal::Queue::pop(q, result);           // count = 2

  fail_unless(test_strings == result,
              "The queue pop was not the expected result.");

  fail_unless(q->count == 2,
              "Queue count was not correct after several operations.");

  free_result = ::circle::internal::Queue::free(q);
  fail_unless(free_result, "Circle context was not null after free.");

  circle::finalize();
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
