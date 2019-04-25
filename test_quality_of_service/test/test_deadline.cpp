// Copyright 2019 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <memory>
#include <string>
#include <tuple>

#include "gtest/gtest.h"

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/executors/single_threaded_executor.hpp"

#include "std_msgs/msg/string.hpp"

#include "test_quality_of_service/qos_test_publisher.hpp"
#include "test_quality_of_service/qos_test_subscriber.hpp"
#include "test_quality_of_service/qos_utilities.hpp"

using namespace std::chrono_literals;

/// Test Deadline with a single subscriber node
TEST_F(QosRclcppTestFixture, test_deadline_no_publisher) {
  const std::chrono::milliseconds deadline_duration = 1s;
  const std::chrono::milliseconds test_duration = 10500ms;
  const std::tuple<size_t, size_t> deadline_duration_tuple = convert_chrono_milliseconds_to_size_t(
    deadline_duration);
  const int expected_number_of_deadline_callbacks = test_duration / deadline_duration;

  int total_number_of_subscriber_deadline_events = 0;
  int last_sub_count = 0;

  rmw_qos_profile_t qos_profile = rmw_qos_profile_default;
  qos_profile.deadline.sec = std::get<0>(deadline_duration_tuple);
  qos_profile.deadline.nsec = std::get<1>(deadline_duration_tuple);

  // setup subscription options and callback
  rclcpp::SubscriptionOptions<> subscriber_options;
  subscriber_options.qos_profile = qos_profile;
  subscriber_options.event_callbacks.deadline_callback =
    [&last_sub_count,
    &total_number_of_subscriber_deadline_events](rclcpp::QOSDeadlineRequestedInfo & event) -> void
    {
      RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "QOSDeadlineRequestedInfo callback");
      total_number_of_subscriber_deadline_events++;
      // assert the correct value on a change
      ASSERT_EQ(1, event.total_count_change);
      last_sub_count = event.total_count;
    };

  rclcpp::PublisherOptions<> publisher_options;

  const std::string topic("test_deadline_no_publisher");

  // register a publisher for the topic but don't publish anything
  auto publisher = std::make_shared<QosTestPublisher>("publisher", topic, publisher_options,
      test_duration);
  auto subscriber = std::make_shared<QosTestSubscriber>("subscriber", topic, subscriber_options);

  executor->add_node(subscriber);
  subscriber->start();

  // the future will never be resolved, so simply time out to force the experiment to stop
  executor->spin_until_future_complete(dummy_future, test_duration);
  subscriber->teardown();

  EXPECT_EQ(subscriber->get_count(), 0);
  EXPECT_EQ(publisher->get_count(), 0);
  EXPECT_EQ(last_sub_count, expected_number_of_deadline_callbacks);
  EXPECT_EQ(total_number_of_subscriber_deadline_events, expected_number_of_deadline_callbacks);
}

/// Test Deadline with a single publishing node and single subscriber node
TEST_F(QosRclcppTestFixture, test_deadline) {
  int expected_number_of_pub_events = 5;
  const std::chrono::milliseconds deadline_duration = 1s;
  const std::tuple<size_t, size_t> deadline_duration_tuple = convert_chrono_milliseconds_to_size_t(
    deadline_duration);
  const std::chrono::milliseconds max_test_length = 10s;
  const std::chrono::milliseconds publish_rate = deadline_duration / expected_number_of_pub_events;

  // used for lambda capture
  int total_number_of_publisher_deadline_events = 0;
  int total_number_of_subscriber_deadline_events = 0;
  int last_pub_count = 0;
  int last_sub_count = 0;

  rmw_qos_profile_t qos_profile = rmw_qos_profile_default;
  qos_profile.deadline.sec = std::get<0>(deadline_duration_tuple);
  qos_profile.deadline.nsec = std::get<1>(deadline_duration_tuple);

  // setup subscription options and callback
  rclcpp::SubscriptionOptions<> subscriber_options;
  subscriber_options.qos_profile = qos_profile;
  subscriber_options.event_callbacks.deadline_callback =
    [&last_sub_count,
    &total_number_of_subscriber_deadline_events](rclcpp::QOSDeadlineRequestedInfo & event) -> void
    {
      RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "QOSDeadlineRequestedInfo callback");
      total_number_of_subscriber_deadline_events++;
      // assert the correct value on a change
      ASSERT_EQ(1, event.total_count_change);
      last_sub_count = event.total_count;
    };

  // setup publishing options and callback
  rclcpp::PublisherOptions<> publisher_options;
  publisher_options.qos_profile = qos_profile;
  publisher_options.event_callbacks.deadline_callback =
    [&last_pub_count,
    &total_number_of_publisher_deadline_events](rclcpp::QOSDeadlineOfferedInfo & event) -> void
    {
      RCLCPP_INFO(rclcpp::get_logger("rclcpp"), "QOSDeadlineOfferedInfo callback");
      total_number_of_publisher_deadline_events++;
      // assert the correct value on a change
      ASSERT_EQ(1, event.total_count_change);
      last_pub_count = event.total_count;
    };

  const std::string topic("test_deadline");

  auto publisher = std::make_shared<QosTestPublisher>("publisher", topic, publisher_options,
      publish_rate);
  auto subscriber = std::make_shared<QosTestSubscriber>("subscriber", topic, subscriber_options);

  // toggle publishing on and off to force deadline events
  rclcpp::TimerBase::SharedPtr toggle_publisher_timer = subscriber->create_wall_timer(
    max_test_length / expected_number_of_pub_events,
    [&publisher]() -> void {
      // start / stop publishing to trigger deadline
      publisher->toggle();
    });

  executor->add_node(subscriber);
  executor->add_node(publisher);

  subscriber->start();
  publisher->start();

  // the future will never be resolved, so simply time out to force the experiment to stop
  executor->spin_until_future_complete(dummy_future, max_test_length);
  toggle_publisher_timer->cancel();
  subscriber->teardown();
  publisher->teardown();

  EXPECT_GT(publisher->get_count(), 0);  // check if we published anything
  EXPECT_GT(subscriber->get_count(), 0);  // check if we received anything

  // check to see if callbacks fired as expected
  EXPECT_EQ(expected_number_of_pub_events, total_number_of_subscriber_deadline_events);
  EXPECT_EQ((expected_number_of_pub_events - 1), total_number_of_publisher_deadline_events);

  // check values reported by the callback
  EXPECT_EQ(expected_number_of_pub_events - 1, last_pub_count);
  EXPECT_EQ((expected_number_of_pub_events), last_sub_count);
}
