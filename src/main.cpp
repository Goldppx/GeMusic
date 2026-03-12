#include <iostream>
#include <string>
#include <string_view>

#include "gemusic/auth/login_manager.h"
#include "gemusic/config/settings.h"
#include "gemusic/logger.h"
#include "gemusic/player/music_player.h"
#include "gemusic/ui/app_ui.h"

// 解析命令行参数，返回是否启用 verbose 模式
// 支持：--verbose / -v
static auto ParseArgs(int argc, char** argv) -> bool {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--verbose" || arg == "-v") {
            return true;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "用法: GeMusic [选项]\n"
                      << "\n"
                      << "选项:\n"
                      << "  -v, --verbose    将调试日志写入 /tmp/gemusic_debug.log\n"
                      << "  -h, --help       显示此帮助信息\n";
            std::exit(0);
        }
    }
    return false;
}

int main(int argc, char** argv) {
    // 解析命令行参数
    const bool verbose = ParseArgs(argc, argv);

    // 初始化日志（verbose 时写文件，否则 null sink）
    gemusic::InitLogger(verbose);

    // 默认配置文件路径
    const auto config_path = gemusic::config::GetDefaultConfigPath();

    // 加载配置，失败则使用默认配置
    gemusic::config::Settings settings;
    auto config_result = gemusic::config::LoadSettings(config_path);
    if (config_result) {
        settings = std::move(config_result.value());
        spdlog::info("配置加载成功: {}", config_path);
    } else {
        std::cerr << "警告: " << config_result.error().message << std::endl;
        std::cerr << "将使用默认配置启动" << std::endl;
        spdlog::warn("配置加载失败，使用默认配置: {}", config_result.error().message);
    }

    // 初始化播放器
    gemusic::player::MusicPlayer player;
    player.SetVolume(settings.volume);
    spdlog::info("播放器初始化完成，音量: {}", settings.volume);

    // LoginManager 的状态回调在构造 AppUi 之后才能使用 screen.PostEvent，
    // 因此先用空回调构造，再在 AppUi 内部注册真正的刷新回调。
    gemusic::auth::LoginManager login_manager(settings, config_path, [] {});

    spdlog::info("GeMusic 启动");

    // 启动 TUI 界面，传入配置、播放器和登录管理器
    gemusic::ui::AppUi app(settings, player, login_manager, config_path);
    app.Run();

    spdlog::info("GeMusic 退出");
    return 0;
}
