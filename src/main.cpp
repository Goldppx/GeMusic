#include <iostream>
#include <string>

#include "gemusic/config/settings.h"
#include "gemusic/ui/app_ui.h"

int main() {
    // 默认配置文件路径
    constexpr auto kConfigPath = "config/gemusic.yaml";

    // 尝试加载配置文件
    auto config_result = gemusic::config::LoadSettings(kConfigPath);
    if (!config_result) {
        std::cerr << "警告: " << config_result.error().message << std::endl;
        std::cerr << "将使用默认配置启动" << std::endl;
    }

    // 启动 TUI 界面
    gemusic::ui::AppUi app;
    app.Run();

    return 0;
}
