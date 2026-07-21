// Auto-generated gtest macros only

#define ASSERT_ANY_THROW(statement) GTEST_TEST_ANY_THROW_(statement, GTEST_FATAL_FAILURE_)
#define ASSERT_DEATH(statement,matcher) ASSERT_EXIT(statement, ::testing::internal::ExitedUnsuccessfully, matcher)
#define ASSERT_DEATH_IF_SUPPORTED(statement,regex) ASSERT_DEATH(statement, regex)
#define ASSERT_DEBUG_DEATH(statement,regex) ASSERT_DEATH(statement, regex)
#define ASSERT_DOUBLE_EQ(val1,val2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperFloatingPointEQ<double>, val1, val2)
#define ASSERT_EQ(val1,val2) GTEST_ASSERT_EQ(val1, val2)
#define ASSERT_EXIT(statement,predicate,matcher) GTEST_DEATH_TEST_(statement, predicate, matcher, GTEST_FATAL_FAILURE_)
#define ASSERT_FALSE(condition) GTEST_ASSERT_FALSE(condition)
#define ASSERT_FLOAT_EQ(val1,val2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperFloatingPointEQ<float>, val1, val2)
#define ASSERT_GE(val1,val2) GTEST_ASSERT_GE(val1, val2)
#define ASSERT_GT(val1,val2) GTEST_ASSERT_GT(val1, val2)
#define ASSERT_LE(val1,val2) GTEST_ASSERT_LE(val1, val2)
#define ASSERT_LT(val1,val2) GTEST_ASSERT_LT(val1, val2)
#define ASSERT_NE(val1,val2) GTEST_ASSERT_NE(val1, val2)
#define ASSERT_NEAR(val1,val2,abs_error) ASSERT_PRED_FORMAT3(::testing::internal::DoubleNearPredFormat, val1, val2, abs_error)
#define ASSERT_NO_FATAL_FAILURE(statement) GTEST_TEST_NO_FATAL_FAILURE_(statement, GTEST_FATAL_FAILURE_)
#define ASSERT_NO_THROW(statement) GTEST_TEST_NO_THROW_(statement, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED1(pred,v1) GTEST_PRED1_(pred, v1, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED2(pred,v1,v2) GTEST_PRED2_(pred, v1, v2, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED3(pred,v1,v2,v3) GTEST_PRED3_(pred, v1, v2, v3, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED4(pred,v1,v2,v3,v4) GTEST_PRED4_(pred, v1, v2, v3, v4, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED5(pred,v1,v2,v3,v4,v5) GTEST_PRED5_(pred, v1, v2, v3, v4, v5, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED_FORMAT1(pred_format,v1) GTEST_PRED_FORMAT1_(pred_format, v1, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED_FORMAT2(pred_format,v1,v2) GTEST_PRED_FORMAT2_(pred_format, v1, v2, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED_FORMAT3(pred_format,v1,v2,v3) GTEST_PRED_FORMAT3_(pred_format, v1, v2, v3, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED_FORMAT4(pred_format,v1,v2,v3,v4) GTEST_PRED_FORMAT4_(pred_format, v1, v2, v3, v4, GTEST_FATAL_FAILURE_)
#define ASSERT_PRED_FORMAT5(pred_format,v1,v2,v3,v4,v5) GTEST_PRED_FORMAT5_(pred_format, v1, v2, v3, v4, v5, GTEST_FATAL_FAILURE_)
#define ASSERT_STRCASEEQ(s1,s2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperSTRCASEEQ, s1, s2)
#define ASSERT_STRCASENE(s1,s2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperSTRCASENE, s1, s2)
#define ASSERT_STREQ(s1,s2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperSTREQ, s1, s2)
#define ASSERT_STRNE(s1,s2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperSTRNE, s1, s2)
#define ASSERT_THROW(statement,expected_exception) GTEST_TEST_THROW_(statement, expected_exception, GTEST_FATAL_FAILURE_)
#define ASSERT_TRUE(condition) GTEST_ASSERT_TRUE(condition)
#define EXPECT_ANY_THROW(statement) GTEST_TEST_ANY_THROW_(statement, GTEST_NONFATAL_FAILURE_)
#define EXPECT_DEATH(statement,matcher) EXPECT_EXIT(statement, ::testing::internal::ExitedUnsuccessfully, matcher)
#define EXPECT_DEATH_IF_SUPPORTED(statement,regex) EXPECT_DEATH(statement, regex)
#define EXPECT_DEBUG_DEATH(statement,regex) EXPECT_DEATH(statement, regex)
#define EXPECT_DOUBLE_EQ(val1,val2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperFloatingPointEQ<double>, val1, val2)
#define EXPECT_EQ(val1,val2) EXPECT_PRED_FORMAT2(::testing::internal::EqHelper::Compare, val1, val2)
#define EXPECT_EXIT(statement,predicate,matcher) GTEST_DEATH_TEST_(statement, predicate, matcher, GTEST_NONFATAL_FAILURE_)
#define EXPECT_FALSE(condition) GTEST_EXPECT_FALSE(condition)
#define EXPECT_FLOAT_EQ(val1,val2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperFloatingPointEQ<float>, val1, val2)
#define EXPECT_GE(val1,val2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperGE, val1, val2)
#define EXPECT_GT(val1,val2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperGT, val1, val2)
#define EXPECT_LE(val1,val2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperLE, val1, val2)
#define EXPECT_LT(val1,val2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperLT, val1, val2)
#define EXPECT_NE(val1,val2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperNE, val1, val2)
#define EXPECT_NEAR(val1,val2,abs_error) EXPECT_PRED_FORMAT3(::testing::internal::DoubleNearPredFormat, val1, val2, abs_error)
#define EXPECT_NO_FATAL_FAILURE(statement) GTEST_TEST_NO_FATAL_FAILURE_(statement, GTEST_NONFATAL_FAILURE_)
#define EXPECT_NO_THROW(statement) GTEST_TEST_NO_THROW_(statement, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED1(pred,v1) GTEST_PRED1_(pred, v1, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED2(pred,v1,v2) GTEST_PRED2_(pred, v1, v2, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED3(pred,v1,v2,v3) GTEST_PRED3_(pred, v1, v2, v3, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED4(pred,v1,v2,v3,v4) GTEST_PRED4_(pred, v1, v2, v3, v4, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED5(pred,v1,v2,v3,v4,v5) GTEST_PRED5_(pred, v1, v2, v3, v4, v5, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED_FORMAT1(pred_format,v1) GTEST_PRED_FORMAT1_(pred_format, v1, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED_FORMAT2(pred_format,v1,v2) GTEST_PRED_FORMAT2_(pred_format, v1, v2, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED_FORMAT3(pred_format,v1,v2,v3) GTEST_PRED_FORMAT3_(pred_format, v1, v2, v3, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED_FORMAT4(pred_format,v1,v2,v3,v4) GTEST_PRED_FORMAT4_(pred_format, v1, v2, v3, v4, GTEST_NONFATAL_FAILURE_)
#define EXPECT_PRED_FORMAT5(pred_format,v1,v2,v3,v4,v5) GTEST_PRED_FORMAT5_(pred_format, v1, v2, v3, v4, v5, GTEST_NONFATAL_FAILURE_)
#define EXPECT_STRCASEEQ(s1,s2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperSTRCASEEQ, s1, s2)
#define EXPECT_STRCASENE(s1,s2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperSTRCASENE, s1, s2)
#define EXPECT_STREQ(s1,s2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperSTREQ, s1, s2)
#define EXPECT_STRNE(s1,s2) EXPECT_PRED_FORMAT2(::testing::internal::CmpHelperSTRNE, s1, s2)
#define EXPECT_THROW(statement,expected_exception) GTEST_TEST_THROW_(statement, expected_exception, GTEST_NONFATAL_FAILURE_)
#define EXPECT_TRUE(condition) GTEST_EXPECT_TRUE(condition)
#define FRIEND_TEST(test_case_name,test_name) friend class test_case_name##_##test_name##_Test
#define GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(T) namespace gtest_do_not_use_outside_namespace_scope {} static const ::testing::internal::MarkAsIgnored gtest_allow_ignore_##T( GTEST_STRINGIFY_(T))
#define GTEST_AMBIGUOUS_ELSE_BLOCKER_ switch (0) case 0: default:
#define GTEST_API_ __attribute__((visibility("default")))
#define GTEST_ASSERT_(expression,on_failure) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (const ::testing::AssertionResult gtest_ar = (expression)) ; else on_failure(gtest_ar.failure_message())
#define GTEST_ASSERT_EQ(val1,val2) ASSERT_PRED_FORMAT2(::testing::internal::EqHelper::Compare, val1, val2)
#define GTEST_ASSERT_FALSE(condition) GTEST_TEST_BOOLEAN_(!(condition), #condition, true, false, GTEST_FATAL_FAILURE_)
#define GTEST_ASSERT_GE(val1,val2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperGE, val1, val2)
#define GTEST_ASSERT_GT(val1,val2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperGT, val1, val2)
#define GTEST_ASSERT_LE(val1,val2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperLE, val1, val2)
#define GTEST_ASSERT_LT(val1,val2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperLT, val1, val2)
#define GTEST_ASSERT_NE(val1,val2) ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperNE, val1, val2)
#define GTEST_ASSERT_TRUE(condition) GTEST_TEST_BOOLEAN_(condition, #condition, false, true, GTEST_FATAL_FAILURE_)
#define GTEST_ATTRIBUTE_NO_SANITIZE_ADDRESS_ 
#define GTEST_ATTRIBUTE_NO_SANITIZE_HWADDRESS_ 
#define GTEST_ATTRIBUTE_NO_SANITIZE_MEMORY_ 
#define GTEST_ATTRIBUTE_NO_SANITIZE_THREAD_ 
#define GTEST_ATTRIBUTE_PRINTF_(string_index,first_to_check) __attribute__((__format__(__printf__, string_index, first_to_check)))
#define GTEST_ATTRIBUTE_UNUSED_ __attribute__((unused))
#define GTEST_BIND_(TmplSel,T) TmplSel::template Bind<T>::type
#define GTEST_CAN_STREAM_RESULTS_ 1
#define GTEST_CHECK_(condition) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (::testing::internal::IsTrue(condition)) ; else GTEST_LOG_(FATAL) << "Condition " #condition " failed. "
#define GTEST_CHECK_POSIX_SUCCESS_(posix_call) if (const int gtest_error = (posix_call)) GTEST_LOG_(FATAL) << #posix_call << "failed with error " << gtest_error
#define GTEST_CONCAT_TOKEN_(foo,bar) GTEST_CONCAT_TOKEN_IMPL_(foo, bar)
#define GTEST_CONCAT_TOKEN_IMPL_(foo,bar) foo##bar
#define GTEST_DEATH_TEST_(statement,predicate,regex_or_matcher,fail) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (::testing::internal::AlwaysTrue()) { ::testing::internal::DeathTest* gtest_dt; if (!::testing::internal::DeathTest::Create( #statement, ::testing::internal::MakeDeathTestMatcher(regex_or_matcher), __FILE__, __LINE__, &gtest_dt)) { goto GTEST_CONCAT_TOKEN_(gtest_label_, __LINE__); } if (gtest_dt != nullptr) { std::unique_ptr< ::testing::internal::DeathTest> gtest_dt_ptr(gtest_dt); switch (gtest_dt->AssumeRole()) { case ::testing::internal::DeathTest::OVERSEE_TEST: if (!gtest_dt->Passed(predicate(gtest_dt->Wait()))) { goto GTEST_CONCAT_TOKEN_(gtest_label_, __LINE__); } break; case ::testing::internal::DeathTest::EXECUTE_TEST: { ::testing::internal::DeathTest::ReturnSentinel gtest_sentinel( gtest_dt); GTEST_EXECUTE_DEATH_TEST_STATEMENT_(statement, gtest_dt); gtest_dt->Abort(::testing::internal::DeathTest::TEST_DID_NOT_DIE); break; } } } } else GTEST_CONCAT_TOKEN_(gtest_label_, __LINE__) : fail(::testing::internal::DeathTest::LastMessage())
#define GTEST_DECLARE_STATIC_MUTEX_(mutex) extern ::testing::internal::MutexBase mutex
#define GTEST_DECLARE_bool_(name) namespace testing { GTEST_API_ extern bool GTEST_FLAG(name); } static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DECLARE_int32_(name) namespace testing { GTEST_API_ extern std::int32_t GTEST_FLAG(name); } static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DECLARE_string_(name) namespace testing { GTEST_API_ extern ::std::string GTEST_FLAG(name); } static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DEFAULT_DEATH_TEST_STYLE "fast"
#define GTEST_DEFINE_STATIC_MUTEX_(mutex) ::testing::internal::MutexBase mutex = {PTHREAD_MUTEX_INITIALIZER, false, 0}
#define GTEST_DEFINE_bool_(name,default_val,doc) namespace testing { GTEST_API_ bool GTEST_FLAG(name) = (default_val); } static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DEFINE_int32_(name,default_val,doc) namespace testing { GTEST_API_ std::int32_t GTEST_FLAG(name) = (default_val); } static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DEFINE_string_(name,default_val,doc) namespace testing { GTEST_API_ ::std::string GTEST_FLAG(name) = (default_val); } static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DEV_EMAIL_ "googletestframework@@googlegroups.com"
#define GTEST_DISABLE_MSC_DEPRECATED_POP_() _Pragma("clang diagnostic pop")
#define GTEST_DISABLE_MSC_DEPRECATED_PUSH_() _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"") _Pragma("clang diagnostic ignored \"-Wdeprecated-implementations\"")
#define GTEST_DISABLE_MSC_WARNINGS_POP_() 
#define GTEST_DISABLE_MSC_WARNINGS_PUSH_(warnings) 
#define GTEST_EXCEPTION_TYPE_(e) ::testing::internal::GetTypeName(typeid(e))
#define GTEST_EXCLUSIVE_LOCK_REQUIRED_(locks) 
#define GTEST_EXECUTE_DEATH_TEST_STATEMENT_(statement,death_test) try { GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(statement); } catch (const ::std::exception& gtest_exception) { fprintf( stderr, "\n%s: Caught std::exception-derived exception escaping the " "death test statement. Exception message: %s\n", ::testing::internal::FormatFileLocation(__FILE__, __LINE__).c_str(), gtest_exception.what()); fflush(stderr); death_test->Abort(::testing::internal::DeathTest::TEST_THREW_EXCEPTION); } catch (...) { death_test->Abort(::testing::internal::DeathTest::TEST_THREW_EXCEPTION); }
#define GTEST_EXECUTE_STATEMENT_(statement,regex_or_matcher) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (::testing::internal::AlwaysTrue()) { GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(statement); } else if (!::testing::internal::AlwaysTrue()) { ::testing::internal::MakeDeathTestMatcher(regex_or_matcher); } else ::testing::Message()
#define GTEST_EXPAND_(arg) arg
#define GTEST_EXPECT_FALSE(condition) GTEST_TEST_BOOLEAN_(!(condition), #condition, true, false, GTEST_NONFATAL_FAILURE_)
#define GTEST_EXPECT_TRUE(condition) GTEST_TEST_BOOLEAN_(condition, #condition, false, true, GTEST_NONFATAL_FAILURE_)
#define GTEST_FAIL() GTEST_FATAL_FAILURE_("Failed")
#define GTEST_FAIL_AT(file,line) GTEST_MESSAGE_AT_(file, line, "Failed", ::testing::TestPartResult::kFatalFailure)
#define GTEST_FATAL_FAILURE_(message) return GTEST_MESSAGE_(message, ::testing::TestPartResult::kFatalFailure)
#define GTEST_FLAG(name) FLAGS_gtest_##name
#define GTEST_FLAG_GET(name) ::testing::GTEST_FLAG(name)
#define GTEST_FLAG_NAME_(name) gtest_##name
#define GTEST_FLAG_PREFIX_ "gtest_"
#define GTEST_FLAG_PREFIX_DASH_ "gtest-"
#define GTEST_FLAG_PREFIX_UPPER_ "GTEST_"
#define GTEST_FLAG_SAVER_ ::testing::internal::GTestFlagSaver
#define GTEST_FLAG_SET(name,value) (void)(::testing::GTEST_FLAG(name) = value)
#define GTEST_GCC_VER_ (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#define GTEST_GET_FIRST_(first,...) first
#define GTEST_GET_SECOND_(first,second,...) second
#define GTEST_HAS_ALT_PATH_SEP_ 0
#define GTEST_HAS_CLONE 1
#define GTEST_HAS_CXXABI_H_ 1
#define GTEST_HAS_DEATH_TEST 1
#define GTEST_HAS_EXCEPTIONS (__EXCEPTIONS && __has_feature(cxx_exceptions))
#define GTEST_HAS_POSIX_RE (!GTEST_OS_WINDOWS && !GTEST_OS_XTENSA)
#define GTEST_HAS_PTHREAD (GTEST_OS_LINUX || GTEST_OS_MAC || GTEST_OS_HPUX || GTEST_OS_QNX || GTEST_OS_FREEBSD || GTEST_OS_NACL || GTEST_OS_NETBSD || GTEST_OS_FUCHSIA || GTEST_OS_DRAGONFLY || GTEST_OS_GNU_KFREEBSD || GTEST_OS_OPENBSD || GTEST_OS_HAIKU || GTEST_OS_GNU_HURD)
#define GTEST_HAS_RTTI 1
#define GTEST_HAS_SEH 0
#define GTEST_HAS_STD_WSTRING (!(GTEST_OS_LINUX_ANDROID || GTEST_OS_CYGWIN || GTEST_OS_SOLARIS || GTEST_OS_HAIKU || GTEST_OS_ESP32 || GTEST_OS_ESP8266 || GTEST_OS_XTENSA))
#define GTEST_HAS_STREAM_REDIRECTION 1
#define GTEST_HAS_TYPED_TEST 1
#define GTEST_HAS_TYPED_TEST_P 1
#define GTEST_INIT_GOOGLE_TEST_NAME_ "testing::InitGoogleTest"
#define GTEST_INTENTIONAL_CONST_COND_POP_() GTEST_DISABLE_MSC_WARNINGS_POP_()
#define GTEST_INTENTIONAL_CONST_COND_PUSH_() GTEST_DISABLE_MSC_WARNINGS_PUSH_(4127)
#define GTEST_INTERNAL_DEPRECATED(message) __attribute__((deprecated(message)))
#define GTEST_INTERNAL_HAS_ANY 1
#define GTEST_INTERNAL_HAS_OPTIONAL 1
#define GTEST_INTERNAL_HAS_STRING_VIEW 1
#define GTEST_INTERNAL_HAS_VARIANT 1
#define GTEST_IS_THREADSAFE (GTEST_HAS_MUTEX_AND_THREAD_LOCAL_ || (GTEST_OS_WINDOWS && !GTEST_OS_WINDOWS_PHONE && !GTEST_OS_WINDOWS_RT) || GTEST_HAS_PTHREAD)
#define GTEST_LOCK_EXCLUDED_(locks) 
#define GTEST_LOG_(severity) ::testing::internal::GTestLog(::testing::internal::GTEST_##severity, __FILE__, __LINE__) .GetStream()
#define GTEST_MAYBE_5046_ 
#define GTEST_MESSAGE_(message,result_type) GTEST_MESSAGE_AT_(__FILE__, __LINE__, message, result_type)
#define GTEST_MESSAGE_AT_(file,line,message,result_type) ::testing::internal::AssertHelper(result_type, file, line, message) = ::testing::Message()
#define GTEST_MUST_USE_RESULT_ __attribute__((warn_unused_result))
#define GTEST_NAME_ "Google Test"
#define GTEST_NAME_GENERATOR_(TestSuiteName) gtest_type_params_##TestSuiteName##_NameGenerator
#define GTEST_NONFATAL_FAILURE_(message) GTEST_MESSAGE_(message, ::testing::TestPartResult::kNonFatalFailure)
#define GTEST_NO_INLINE_ __attribute__((noinline))
#define GTEST_NO_TAIL_CALL_ __attribute__((disable_tail_calls))
#define GTEST_OS_LINUX 1
#define GTEST_PATH_SEP_ "/"
#define GTEST_PRED1_(pred,v1,on_failure) GTEST_ASSERT_(::testing::AssertPred1Helper(#pred, #v1, pred, v1), on_failure)
#define GTEST_PRED2_(pred,v1,v2,on_failure) GTEST_ASSERT_(::testing::AssertPred2Helper(#pred, #v1, #v2, pred, v1, v2), on_failure)
#define GTEST_PRED3_(pred,v1,v2,v3,on_failure) GTEST_ASSERT_( ::testing::AssertPred3Helper(#pred, #v1, #v2, #v3, pred, v1, v2, v3), on_failure)
#define GTEST_PRED4_(pred,v1,v2,v3,v4,on_failure) GTEST_ASSERT_(::testing::AssertPred4Helper(#pred, #v1, #v2, #v3, #v4, pred, v1, v2, v3, v4), on_failure)
#define GTEST_PRED5_(pred,v1,v2,v3,v4,v5,on_failure) GTEST_ASSERT_(::testing::AssertPred5Helper(#pred, #v1, #v2, #v3, #v4, #v5, pred, v1, v2, v3, v4, v5), on_failure)
#define GTEST_PRED_FORMAT1_(pred_format,v1,on_failure) GTEST_ASSERT_(pred_format(#v1, v1), on_failure)
#define GTEST_PRED_FORMAT2_(pred_format,v1,v2,on_failure) GTEST_ASSERT_(pred_format(#v1, #v2, v1, v2), on_failure)
#define GTEST_PRED_FORMAT3_(pred_format,v1,v2,v3,on_failure) GTEST_ASSERT_(pred_format(#v1, #v2, #v3, v1, v2, v3), on_failure)
#define GTEST_PRED_FORMAT4_(pred_format,v1,v2,v3,v4,on_failure) GTEST_ASSERT_(pred_format(#v1, #v2, #v3, #v4, v1, v2, v3, v4), on_failure)
#define GTEST_PRED_FORMAT5_(pred_format,v1,v2,v3,v4,v5,on_failure) GTEST_ASSERT_(pred_format(#v1, #v2, #v3, #v4, #v5, v1, v2, v3, v4, v5), on_failure)
#define GTEST_PROJECT_URL_ "https://github.com/google/googletest/"
#define GTEST_REFERENCE_TO_CONST_(T) typename ::testing::internal::ConstRef<T>::type
#define GTEST_REGISTERED_TEST_NAMES_(TestSuiteName) gtest_registered_test_names_##TestSuiteName##_
#define GTEST_REMOVE_REFERENCE_AND_CONST_(T) typename std::remove_const<typename std::remove_reference<T>::type>::type
#define GTEST_SKIP() GTEST_SKIP_("")
#define GTEST_SKIP_(message) return GTEST_MESSAGE_(message, ::testing::TestPartResult::kSkip)
#define GTEST_SNPRINTF_ snprintf
#define GTEST_STRINGIFY_(...) GTEST_STRINGIFY_HELPER_(__VA_ARGS__, )
#define GTEST_STRINGIFY_HELPER_(name,...) #name
#define GTEST_SUCCEED() GTEST_SUCCESS_("Succeeded")
#define GTEST_SUCCESS_(message) GTEST_MESSAGE_(message, ::testing::TestPartResult::kSuccess)
#define GTEST_SUITE_NAMESPACE_(TestSuiteName) gtest_suite_##TestSuiteName##_
#define GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(statement) if (::testing::internal::AlwaysTrue()) { statement; } else static_assert(true, "")
#define GTEST_TEMPLATE_ template <typename T> class
#define GTEST_TEST(test_suite_name,test_name) GTEST_TEST_(test_suite_name, test_name, ::testing::Test, ::testing::internal::GetTestTypeId())
#define GTEST_TEST_(test_suite_name,test_name,parent_class,parent_id) static_assert(sizeof(GTEST_STRINGIFY_(test_suite_name)) > 1, "test_suite_name must not be empty"); static_assert(sizeof(GTEST_STRINGIFY_(test_name)) > 1, "test_name must not be empty"); class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) : public parent_class { public: GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() = default; ~GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() override = default; GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) (const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) = delete; GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) & operator=( const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) = delete; GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) (GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &&) noexcept = delete; GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) & operator=( GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &&) noexcept = delete; private: void TestBody() override; static ::testing::TestInfo* const test_info_ GTEST_ATTRIBUTE_UNUSED_; }; ::testing::TestInfo* const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::test_info_ = ::testing::internal::MakeAndRegisterTestInfo( #test_suite_name, #test_name, nullptr, nullptr, ::testing::internal::CodeLocation(__FILE__, __LINE__), (parent_id), ::testing::internal::SuiteApiResolver< parent_class>::GetSetUpCaseOrSuite(__FILE__, __LINE__), ::testing::internal::SuiteApiResolver< parent_class>::GetTearDownCaseOrSuite(__FILE__, __LINE__), new ::testing::internal::TestFactoryImpl<GTEST_TEST_CLASS_NAME_( test_suite_name, test_name)>); void GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::TestBody()
#define GTEST_TEST_ANY_THROW_(statement,fail) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (::testing::internal::AlwaysTrue()) { bool gtest_caught_any = false; try { GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(statement); } catch (...) { gtest_caught_any = true; } if (!gtest_caught_any) { goto GTEST_CONCAT_TOKEN_(gtest_label_testanythrow_, __LINE__); } } else GTEST_CONCAT_TOKEN_(gtest_label_testanythrow_, __LINE__) : fail("Expected: " #statement " throws an exception.\n" "  Actual: it doesn't.")
#define GTEST_TEST_BOOLEAN_(expression,text,actual,expected,fail) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (const ::testing::AssertionResult gtest_ar_ = ::testing::AssertionResult(expression)) ; else fail(::testing::internal::GetBoolAssertionFailureMessage( gtest_ar_, text, #actual, #expected) .c_str())
#define GTEST_TEST_CLASS_NAME_(test_suite_name,test_name) test_suite_name##_##test_name##_Test
#define GTEST_TEST_F(test_fixture,test_name) GTEST_TEST_(test_fixture, test_name, test_fixture, ::testing::internal::GetTypeId<test_fixture>())
#define GTEST_TEST_NO_FATAL_FAILURE_(statement,fail) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (::testing::internal::AlwaysTrue()) { ::testing::internal::HasNewFatalFailureHelper gtest_fatal_failure_checker; GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(statement); if (gtest_fatal_failure_checker.has_new_fatal_failure()) { goto GTEST_CONCAT_TOKEN_(gtest_label_testnofatal_, __LINE__); } } else GTEST_CONCAT_TOKEN_(gtest_label_testnofatal_, __LINE__) : fail("Expected: " #statement " doesn't generate new fatal " "failures in the current thread.\n" "  Actual: it does.")
#define GTEST_TEST_NO_THROW_(statement,fail) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (::testing::internal::TrueWithString gtest_msg{}) { try { GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(statement); } GTEST_TEST_NO_THROW_CATCH_STD_EXCEPTION_() catch (...) { gtest_msg.value = "it throws."; goto GTEST_CONCAT_TOKEN_(gtest_label_testnothrow_, __LINE__); } } else GTEST_CONCAT_TOKEN_(gtest_label_testnothrow_, __LINE__) : fail(("Expected: " #statement " doesn't throw an exception.\n" "  Actual: " + gtest_msg.value) .c_str())
#define GTEST_TEST_NO_THROW_CATCH_STD_EXCEPTION_() catch (std::exception const& e) { gtest_msg.value = "it throws "; gtest_msg.value += GTEST_EXCEPTION_TYPE_(e); gtest_msg.value += " with description \""; gtest_msg.value += e.what(); gtest_msg.value += "\"."; goto GTEST_CONCAT_TOKEN_(gtest_label_testnothrow_, __LINE__); }
#define GTEST_TEST_THROW_(statement,expected_exception,fail) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (::testing::internal::TrueWithString gtest_msg{}) { bool gtest_caught_expected = false; try { GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(statement); } catch (expected_exception const&) { gtest_caught_expected = true; } GTEST_TEST_THROW_CATCH_STD_EXCEPTION_(statement, expected_exception) catch (...) { gtest_msg.value = "Expected: " #statement " throws an exception of type " #expected_exception ".\n  Actual: it throws a different type."; goto GTEST_CONCAT_TOKEN_(gtest_label_testthrow_, __LINE__); } if (!gtest_caught_expected) { gtest_msg.value = "Expected: " #statement " throws an exception of type " #expected_exception ".\n  Actual: it throws nothing."; goto GTEST_CONCAT_TOKEN_(gtest_label_testthrow_, __LINE__); } } else GTEST_CONCAT_TOKEN_(gtest_label_testthrow_, __LINE__) : fail(gtest_msg.value.c_str())
#define GTEST_TEST_THROW_CATCH_STD_EXCEPTION_(statement,expected_exception) catch (typename std::conditional< std::is_same<typename std::remove_cv<typename std::remove_reference< expected_exception>::type>::type, std::exception>::value, const ::testing::internal::NeverThrown&, const std::exception&>::type e) { gtest_msg.value = "Expected: " #statement " throws an exception of type " #expected_exception ".\n  Actual: it throws "; gtest_msg.value += GTEST_EXCEPTION_TYPE_(e); gtest_msg.value += " with description \""; gtest_msg.value += e.what(); gtest_msg.value += "\"."; goto GTEST_CONCAT_TOKEN_(gtest_label_testthrow_, __LINE__); }
#define GTEST_TYPED_TEST_SUITE_P_STATE_(TestSuiteName) gtest_typed_test_suite_p_state_##TestSuiteName##_
#define GTEST_TYPE_PARAMS_(TestSuiteName) gtest_type_params_##TestSuiteName##_
#define GTEST_UNSUPPORTED_DEATH_TEST(statement,regex,terminator) GTEST_AMBIGUOUS_ELSE_BLOCKER_ if (::testing::internal::AlwaysTrue()) { GTEST_LOG_(WARNING) << "Death tests are not supported on this platform.\n" << "Statement '" #statement "' cannot be verified."; } else if (::testing::internal::AlwaysFalse()) { ::testing::internal::RE::PartialMatch(".*", (regex)); GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(statement); terminator; } else ::testing::Message()
#define GTEST_USES_POSIX_RE 1
#define GTEST_USE_OWN_FLAGFILE_FLAG_ 1
#define GTEST_WIDE_STRING_USES_UTF16_ (GTEST_OS_WINDOWS || GTEST_OS_CYGWIN || GTEST_OS_AIX || GTEST_OS_OS2)
#define INSTANTIATE_TEST_CASE_P static_assert(::testing::internal::InstantiateTestCase_P_IsDeprecated(), ""); INSTANTIATE_TEST_SUITE_P
#define INSTANTIATE_TEST_SUITE_P(prefix,test_suite_name,...) static ::testing::internal::ParamGenerator<test_suite_name::ParamType> gtest_##prefix##test_suite_name##_EvalGenerator_() { return GTEST_EXPAND_(GTEST_GET_FIRST_(__VA_ARGS__, DUMMY_PARAM_)); } static ::std::string gtest_##prefix##test_suite_name##_EvalGenerateName_( const ::testing::TestParamInfo<test_suite_name::ParamType>& info) { if (::testing::internal::AlwaysFalse()) { ::testing::internal::TestNotEmpty(GTEST_EXPAND_(GTEST_GET_SECOND_( __VA_ARGS__, ::testing::internal::DefaultParamName<test_suite_name::ParamType>, DUMMY_PARAM_))); auto t = std::make_tuple(__VA_ARGS__); static_assert(std::tuple_size<decltype(t)>::value <= 2, "Too Many Args!"); } return ((GTEST_EXPAND_(GTEST_GET_SECOND_( __VA_ARGS__, ::testing::internal::DefaultParamName<test_suite_name::ParamType>, DUMMY_PARAM_))))(info); } static int gtest_##prefix##test_suite_name##_dummy_ GTEST_ATTRIBUTE_UNUSED_ = ::testing::UnitTest::GetInstance() ->parameterized_test_registry() .GetTestSuitePatternHolder<test_suite_name>( GTEST_STRINGIFY_(test_suite_name), ::testing::internal::CodeLocation(__FILE__, __LINE__)) ->AddTestSuiteInstantiation( GTEST_STRINGIFY_(prefix), &gtest_##prefix##test_suite_name##_EvalGenerator_, &gtest_##prefix##test_suite_name##_EvalGenerateName_, __FILE__, __LINE__)
#define INSTANTIATE_TYPED_TEST_CASE_P static_assert( ::testing::internal::InstantiateTypedTestCase_P_IsDeprecated(), ""); INSTANTIATE_TYPED_TEST_SUITE_P
#define INSTANTIATE_TYPED_TEST_SUITE_P(Prefix,SuiteName,Types,...) static_assert(sizeof(GTEST_STRINGIFY_(Prefix)) > 1, "test-suit-prefix must not be empty"); static bool gtest_##Prefix##_##SuiteName GTEST_ATTRIBUTE_UNUSED_ = ::testing::internal::TypeParameterizedTestSuite< SuiteName, GTEST_SUITE_NAMESPACE_(SuiteName)::gtest_AllTests_, ::testing::internal::GenerateTypeList<Types>::type>:: Register(GTEST_STRINGIFY_(Prefix), ::testing::internal::CodeLocation(__FILE__, __LINE__), &GTEST_TYPED_TEST_SUITE_P_STATE_(SuiteName), GTEST_STRINGIFY_(SuiteName), GTEST_REGISTERED_TEST_NAMES_(SuiteName), ::testing::internal::GenerateNames< ::testing::internal::NameGeneratorSelector< __VA_ARGS__>::type, ::testing::internal::GenerateTypeList<Types>::type>())
#define REGISTER_TYPED_TEST_CASE_P static_assert(::testing::internal::RegisterTypedTestCase_P_IsDeprecated(), ""); REGISTER_TYPED_TEST_SUITE_P
#define REGISTER_TYPED_TEST_SUITE_P(SuiteName,...) namespace GTEST_SUITE_NAMESPACE_(SuiteName) { typedef ::testing::internal::Templates<__VA_ARGS__> gtest_AllTests_; } static const char* const GTEST_REGISTERED_TEST_NAMES_( SuiteName) GTEST_ATTRIBUTE_UNUSED_ = GTEST_TYPED_TEST_SUITE_P_STATE_(SuiteName).VerifyRegisteredTestNames( GTEST_STRINGIFY_(SuiteName), __FILE__, __LINE__, #__VA_ARGS__)
#define SCOPED_TRACE(message) ::testing::ScopedTrace GTEST_CONCAT_TOKEN_(gtest_trace_, __LINE__)( __FILE__, __LINE__, (message))
#define TEST(test_suite_name,test_name) GTEST_TEST(test_suite_name, test_name)
#define TEST_F(test_fixture,test_name) GTEST_TEST_F(test_fixture, test_name)
#define TEST_P(test_suite_name,test_name) class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) : public test_suite_name { public: GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() {} void TestBody() override; private: static int AddToRegistry() { ::testing::UnitTest::GetInstance() ->parameterized_test_registry() .GetTestSuitePatternHolder<test_suite_name>( GTEST_STRINGIFY_(test_suite_name), ::testing::internal::CodeLocation(__FILE__, __LINE__)) ->AddTestPattern( GTEST_STRINGIFY_(test_suite_name), GTEST_STRINGIFY_(test_name), new ::testing::internal::TestMetaFactory<GTEST_TEST_CLASS_NAME_( test_suite_name, test_name)>(), ::testing::internal::CodeLocation(__FILE__, __LINE__)); return 0; } static int gtest_registering_dummy_ GTEST_ATTRIBUTE_UNUSED_; GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) (const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) = delete; GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) & operator=( const GTEST_TEST_CLASS_NAME_(test_suite_name, test_name) &) = delete; }; int GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::gtest_registering_dummy_ = GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::AddToRegistry(); void GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::TestBody()
#define TYPED_TEST(CaseName,TestName) static_assert(sizeof(GTEST_STRINGIFY_(TestName)) > 1, "test-name must not be empty"); template <typename gtest_TypeParam_> class GTEST_TEST_CLASS_NAME_(CaseName, TestName) : public CaseName<gtest_TypeParam_> { private: typedef CaseName<gtest_TypeParam_> TestFixture; typedef gtest_TypeParam_ TypeParam; void TestBody() override; }; static bool gtest_##CaseName##_##TestName##_registered_ GTEST_ATTRIBUTE_UNUSED_ = ::testing::internal::TypeParameterizedTest< CaseName, ::testing::internal::TemplateSel<GTEST_TEST_CLASS_NAME_(CaseName, TestName)>, GTEST_TYPE_PARAMS_( CaseName)>::Register("", ::testing::internal::CodeLocation( __FILE__, __LINE__), GTEST_STRINGIFY_(CaseName), GTEST_STRINGIFY_(TestName), 0, ::testing::internal::GenerateNames< GTEST_NAME_GENERATOR_(CaseName), GTEST_TYPE_PARAMS_(CaseName)>()); template <typename gtest_TypeParam_> void GTEST_TEST_CLASS_NAME_(CaseName, TestName)<gtest_TypeParam_>::TestBody()
#define TYPED_TEST_CASE static_assert(::testing::internal::TypedTestCaseIsDeprecated(), ""); TYPED_TEST_SUITE
#define TYPED_TEST_CASE_P static_assert(::testing::internal::TypedTestCase_P_IsDeprecated(), ""); TYPED_TEST_SUITE_P
#define TYPED_TEST_P(SuiteName,TestName) namespace GTEST_SUITE_NAMESPACE_(SuiteName) { template <typename gtest_TypeParam_> class TestName : public SuiteName<gtest_TypeParam_> { private: typedef SuiteName<gtest_TypeParam_> TestFixture; typedef gtest_TypeParam_ TypeParam; void TestBody() override; }; static bool gtest_##TestName##_defined_ GTEST_ATTRIBUTE_UNUSED_ = GTEST_TYPED_TEST_SUITE_P_STATE_(SuiteName).AddTestName( __FILE__, __LINE__, GTEST_STRINGIFY_(SuiteName), GTEST_STRINGIFY_(TestName)); } template <typename gtest_TypeParam_> void GTEST_SUITE_NAMESPACE_( SuiteName)::TestName<gtest_TypeParam_>::TestBody()
#define TYPED_TEST_SUITE(CaseName,Types,...) typedef ::testing::internal::GenerateTypeList<Types>::type GTEST_TYPE_PARAMS_(CaseName); typedef ::testing::internal::NameGeneratorSelector<__VA_ARGS__>::type GTEST_NAME_GENERATOR_(CaseName)
#define TYPED_TEST_SUITE_P(SuiteName) static ::testing::internal::TypedTestSuitePState GTEST_TYPED_TEST_SUITE_P_STATE_(SuiteName)
