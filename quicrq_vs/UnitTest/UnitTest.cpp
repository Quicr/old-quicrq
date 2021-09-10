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
	};
}
