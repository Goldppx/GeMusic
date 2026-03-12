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

// 测试保存和加载配置文件的完整流程（含 music_library_path）
TEST_F(SettingsTest, SaveAndLoadRoundTrip) {
    // 构造测试配置
    gemusic::config::Settings settings;
    settings.api_base_url = "https://api.example.com";
    settings.cookies = "test_cookie=abc123";
    settings.volume = 75;
    settings.cache_dir = "/tmp/test_cache";
    settings.music_library_path = "/home/test/Music";

    // 保存配置
    auto save_result = gemusic::config::SaveSettings(settings, kTestConfigPath);
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
    EXPECT_EQ(loaded.music_library_path, settings.music_library_path);
}

// 测试默认 music_library_path 值
TEST_F(SettingsTest, DefaultMusicLibraryPath) {
    gemusic::config::Settings settings;
    EXPECT_EQ(settings.music_library_path, "~/Music");
}

// 测试 ExpandHomePath 展开 ~ 路径
TEST(ExpandHomePathTest, ExpandsTilde) {
    auto result = gemusic::config::ExpandHomePath("~/Music");
    // 展开后不应以 ~ 开头
    EXPECT_FALSE(result.starts_with("~"));
    // 应以 /Music 结尾
    EXPECT_TRUE(result.ends_with("/Music"));
}

// 测试 ExpandHomePath 不展开非 ~ 开头的路径
TEST(ExpandHomePathTest, DoesNotExpandAbsolutePath) {
    auto result = gemusic::config::ExpandHomePath("/usr/local/music");
    EXPECT_EQ(result, "/usr/local/music");
}

// 错误类型测试
TEST(ErrorTest, AppErrorCarriesCodeAndMessage) {
    gemusic::AppError error{gemusic::ErrorCode::kNetworkError, "连接超时"};
    EXPECT_EQ(error.code, gemusic::ErrorCode::kNetworkError);
    EXPECT_EQ(error.message, "连接超时");
}
