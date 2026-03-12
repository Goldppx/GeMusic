#include <gtest/gtest.h>

#include <string>

#include "gemusic/config/settings.h"
#include "gemusic/error.h"

// 配置模块测试
class SettingsTest : public ::testing::Test {
protected:
    const std::string kTestConfigPath = "/tmp/gemusic_test_config.yaml";
};

// 测试加载不存在的配置文件应返回错误
TEST_F(SettingsTest, LoadNonExistentFileReturnsError) {
    auto result = gemusic::config::LoadSettings("/tmp/nonexistent.yaml");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, gemusic::ErrorCode::kFileNotFound);
}

// 测试保存和加载配置文件的完整流程
TEST_F(SettingsTest, SaveAndLoadRoundTrip) {
    // 构造测试配置
    gemusic::config::Settings settings;
    settings.api_base_url = "https://api.example.com";
    settings.cookies = "test_cookie=abc123";
    settings.volume = 75;
    settings.cache_dir = "/tmp/test_cache";

    // 保存配置
    auto save_result =
        gemusic::config::SaveSettings(settings, kTestConfigPath);
    ASSERT_TRUE(save_result.has_value()) << save_result.error().message;

    // 重新加载配置
    auto load_result = gemusic::config::LoadSettings(kTestConfigPath);
    ASSERT_TRUE(load_result.has_value()) << load_result.error().message;

    // 验证字段一致
    const auto& loaded = load_result.value();
    EXPECT_EQ(loaded.api_base_url, settings.api_base_url);
    EXPECT_EQ(loaded.cookies, settings.cookies);
    EXPECT_EQ(loaded.volume, settings.volume);
    EXPECT_EQ(loaded.cache_dir, settings.cache_dir);
}

// 错误类型测试
TEST(ErrorTest, AppErrorCarriesCodeAndMessage) {
    gemusic::AppError error{gemusic::ErrorCode::kNetworkError, "连接超时"};
    EXPECT_EQ(error.code, gemusic::ErrorCode::kNetworkError);
    EXPECT_EQ(error.message, "连接超时");
}
