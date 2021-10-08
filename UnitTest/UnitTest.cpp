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
	};
}
