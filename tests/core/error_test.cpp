#include <gtest/gtest.h>
#include <fmus/core/error.hpp>

namespace fmus::core::test {

TEST(ErrorTest, BasicErrorCode) {
    std::error_code ec = make_error_code(ErrorCode::InvalidArgument);
    EXPECT_EQ(ec.message(), "Invalid argument");
    EXPECT_EQ(ec.category().name(), "fmus");
}

TEST(ErrorTest, ErrorConditionMapping) {
    std::error_code ec = make_error_code(ErrorCode::FileNotFound);
    EXPECT_TRUE(ec == std::errc::no_such_file_or_directory);
}

TEST(ErrorTest, ErrorException) {
    try {
        throw Error(ErrorCode::ConnectionFailed, "Failed to connect to server");
        FAIL() << "Expected Error exception";
    }
    catch (const Error& e) {
        EXPECT_EQ(e.code(), ErrorCode::ConnectionFailed);
        EXPECT_STREQ(e.what(), "Failed to connect to server");
        EXPECT_TRUE(e.error_code() == std::errc::connection_refused);
    }
}

TEST(ErrorTest, ThrowErrorHelper) {
    try {
        throw_error(ErrorCode::OutOfMemory, "Memory allocation failed");
        FAIL() << "Expected Error exception";
    }
    catch (const Error& e) {
        EXPECT_EQ(e.code(), ErrorCode::OutOfMemory);
        EXPECT_STREQ(e.what(), "Memory allocation failed");
        EXPECT_TRUE(e.error_code() == std::errc::not_enough_memory);
    }
}

TEST(ErrorTest, ResultSuccess) {
    Result<int> result = 42;
    EXPECT_TRUE(result.is_ok());
    EXPECT_FALSE(result.is_error());
    EXPECT_EQ(result.value(), 42);
}

TEST(ErrorTest, ResultError) {
    Result<int> result(ErrorCode::InvalidData, "Invalid integer");
    EXPECT_FALSE(result.is_ok());
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.error().code(), ErrorCode::InvalidData);
    EXPECT_THROW(result.value(), Error);
}

TEST(ErrorTest, ResultVoid) {
    Result<void> result;
    EXPECT_TRUE(result.is_ok());
    EXPECT_FALSE(result.is_error());
    EXPECT_NO_THROW(result.value());
}

TEST(ErrorTest, ResultVoidError) {
    Result<void> result(ErrorCode::NotImplemented, "Feature not implemented");
    EXPECT_FALSE(result.is_ok());
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.error().code(), ErrorCode::NotImplemented);
    EXPECT_THROW(result.value(), Error);
}

TEST(ErrorTest, ResultMoveValue) {
    Result<std::unique_ptr<int>> result(std::make_unique<int>(42));
    EXPECT_TRUE(result.is_ok());
    auto ptr = std::move(result).value();
    EXPECT_EQ(*ptr, 42);
}

TEST(ErrorTest, ResultBoolConversion) {
    Result<int> success(42);
    Result<int> failure(ErrorCode::InvalidData, "Invalid integer");

    EXPECT_TRUE(bool(success));
    EXPECT_FALSE(bool(failure));

    if (success) {
        EXPECT_EQ(success.value(), 42);
    }
    else {
        FAIL() << "Expected success";
    }

    if (failure) {
        FAIL() << "Expected failure";
    }
    else {
        EXPECT_EQ(failure.error().code(), ErrorCode::InvalidData);
    }
}

} // namespace fmus::core::test