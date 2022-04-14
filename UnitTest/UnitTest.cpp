#include "pch.h"
#include "CppUnitTest.h"
#include "quicrq_tests.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTest
{
	TEST_CLASS(UnitTest)
	{
	public:

		TEST_METHOD(basic)
		{
			int ret = quicrq_basic_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(basic_rt)
		{
			int ret = quicrq_basic_rt_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(media_video1)
		{
			int ret = quicrq_media_video1_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(media_video1_rt)
		{
			int ret = quicrq_media_video1_rt_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(media_source)
		{
			int ret = quicrq_media_source_test();

			Assert::AreEqual(ret, 0);
		}
		TEST_METHOD(media_source_rt)
		{
			int ret = quicrq_media_source_rt_test();

			Assert::AreEqual(ret, 0);
		}
		TEST_METHOD(media_frame_noloss)
		{
			int ret = quicrq_media_frame_noloss();

			Assert::AreEqual(ret, 0);
		}
		TEST_METHOD(media_frame_loss)
		{
			int ret = quicrq_media_frame_loss();

			Assert::AreEqual(ret, 0);
		}
		TEST_METHOD(datagram_basic)
		{
			int ret = quicrq_datagram_basic_test();

			Assert::AreEqual(ret, 0);
		}
		TEST_METHOD(datagram_loss)
		{
			int ret = quicrq_datagram_loss_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(basic_client) {
			int ret = quicrq_basic_client_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(datagram_client) {
			int ret = quicrq_datagram_client_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(datagram_limit) {
			int ret = quicrq_datagram_limit_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(proto_msg) {
			int ret = proto_msg_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(relay_basic) {
			int ret = quicrq_relay_basic_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(relay_datagram) {
			int ret = quicrq_relay_datagram_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(relay_datagram_loss) {
			int ret = quicrq_relay_datagram_loss_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(relay_basic_client) {
			int ret = quicrq_relay_basic_client_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(relay_datagram_client) {
			int ret = quicrq_relay_datagram_client_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(triangle_basic) {
			int ret = quicrq_triangle_basic_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(triangle_datagram) {
			int ret = quicrq_triangle_datagram_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(triangle_datagram_loss) {
			int ret = quicrq_triangle_datagram_loss_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(pyramid_basic) {
			int ret = quicrq_pyramid_basic_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(pyramid_datagram) {
			int ret = quicrq_pyramid_datagram_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(pyramid_datagram_loss) {
			int ret = quicrq_pyramid_datagram_loss_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(pyramid_datagram_client) {
			int ret = quicrq_pyramid_datagram_client_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(pyramid_datagram_delay) {
			int ret = quicrq_pyramid_datagram_delay_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(pyramid_publish_delay) {
			int ret = quicrq_pyramid_publish_delay_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(twoways_basic) {
			int ret = quicrq_twoways_basic_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(twoways_datagram) {
			int ret = quicrq_twoways_datagram_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(twoways_datagram_loss) {
			int ret = quicrq_twoways_datagram_loss_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(threelegs_basic) {
			int ret = quicrq_threelegs_basic_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(threelegs_datagram) {
			int ret = quicrq_threelegs_datagram_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(threelegs_datagram_loss) {
			int ret = quicrq_threelegs_datagram_loss_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(fourlegs_basic) {
			int ret = quicrq_fourlegs_basic_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(fourlegs_basic_last) {
			int ret = quicrq_fourlegs_basic_last_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(fourlegs_datagram) {
			int ret = quicrq_fourlegs_datagram_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(fourlegs_datagram_last) {
			int ret = quicrq_fourlegs_datagram_last_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(fourlegs_datagram_loss) {
			int ret = quicrq_fourlegs_datagram_loss_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(relay_range) {
			int ret = quick_relay_range_test();

			Assert::AreEqual(ret, 0);
		}

		TEST_METHOD(get_addr) {
			int ret = quicrq_get_addr_test();

			Assert::AreEqual(ret, 0);
		}
	};
}
