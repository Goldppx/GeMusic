#include <iostream>
#include <string>

#include "gemusic/config/settings.h"
#include "gemusic/player/music_player.h"
#include "gemusic/ui/app_ui.h"

int main() {
    // 默认配置文件路径
    const auto config_path = gemusic::config::GetDefaultConfigPath();

    // 加载配置，失败则使用默认配置
    gemusic::config::Settings settings;
    auto config_result = gemusic::config::LoadSettings(config_path);
    if (config_result) {
        settings = std::move(config_result.value());
    } else {
        std::cerr << "警告: " << config_result.error().message << std::endl;
        std::cerr << "将使用默认配置启动" << std::endl;
    }

    // 初始化播放器
    gemusic::player::MusicPlayer player;
    player.SetVolume(settings.volume);

    // 启动 TUI 界面，传入配置和播放器
    gemusic::ui::AppUi app(settings, player, config_path);
    app.Run();

    return 0;
}
