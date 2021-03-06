#include "lanl_circle.hpp"
#include "queue.hpp"
#include <check.h>
#include <stdlib.h>
#include <string>

START_TEST(test_checkpoint_single_write) {
  const std::string test_string = "Here's a test string!";
  std::string result;
  int fakerank = 5;

  int checkpoint_write_ret = -1;

  circle::init(0, NULL);
  circle::internal::Queue out_q(NULL);

  out_q.push(test_string);
  checkpoint_write_ret = out_q.write(fakerank);
  fail_unless(checkpoint_write_ret == 0,
              "The checkpoint write function did not return a positive value.");
}
END_TEST

START_TEST(test_checkpoint_single_read) {
  const std::string test_string = "Here's a test string!";
  std::string result;
  int fakerank = 5;

  int checkpoint_read_ret = -1;

  circle::init(0, NULL);
  circle::internal::Queue in_q(NULL);

  checkpoint_read_ret = in_q.read(fakerank);
  fail_unless(checkpoint_read_ret == 0,
              "The checkpoint read function did not return a positive value.");

  in_q.pop(result);
  fail_unless(in_q.getCount() == 0,
              "Queue count was not correct after poping the last element.");

  fail_unless(test_string == result,
              "Result poped from the queue does not match original.");
}
END_TEST

Suite *check_checkpoint_suite(void) {
  Suite *s = suite_create("check_checkpoint");
  TCase *tc_core = tcase_create("Core");

  tcase_add_test(tc_core, test_checkpoint_single_write);
  tcase_add_test(tc_core, test_checkpoint_single_read);

  suite_add_tcase(s, tc_core);

  return s;
}

int main(void) {
  int number_failed;

  Suite *s = check_checkpoint_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
