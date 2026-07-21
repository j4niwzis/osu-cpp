module;

#include <gtest/gtest.h>

export module gtest;

export namespace testing {
using ::testing::AssertionResult;
using ::testing::Message;
using ::testing::Test;
using ::testing::TestInfo;
using ::testing::TestPartResult;

namespace internal {
using ::testing::internal::AlwaysTrue;
using ::testing::internal::AssertHelper;
using ::testing::internal::CmpHelperFloatingPointEQ;
using ::testing::internal::CmpHelperGE;
using ::testing::internal::CmpHelperGT;
using ::testing::internal::CmpHelperLE;
using ::testing::internal::CmpHelperLT;
using ::testing::internal::CmpHelperNE;
using ::testing::internal::CodeLocation;
using ::testing::internal::DoubleNearPredFormat;
using ::testing::internal::EqHelper;
using ::testing::internal::GetBoolAssertionFailureMessage;
using ::testing::internal::GetTestTypeId;
using ::testing::internal::GetTypeName;
using ::testing::internal::MakeAndRegisterTestInfo;
using ::testing::internal::NeverThrown;
using ::testing::internal::SuiteApiResolver;
using ::testing::internal::TestFactoryImpl;
using ::testing::internal::TrueWithString;
} // namespace internal
} // namespace testing
