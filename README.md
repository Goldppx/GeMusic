# GeMusic — 终端网易云音乐播放器

一个运行在终端里的网易云音乐播放器，基于 FTXUI 构建 TUI 界面，支持在线播放、歌单管理、本地音乐库和歌词显示。

> **截图**
> *(图片待补充)*

---

## 为什么要做这个

网易云音乐的官方客户端体积臃肿、依赖繁重，Linux 版本更新迟缓，且完全不适合终端工作流。对于长期在终端里工作的用户来说，切换到 GUI 播放器听歌是一种打断。
GeMusic 的目标是：在不离开终端的前提下，完整使用网易云音乐的核心功能。

---

## 功能

- 扫码登录 / 账密登录 / Cookie 登录
- 获取并浏览我的歌单，播放任意曲目
- 播放队列管理（替换、追加、移除）
- 本地音乐库扫描与播放（支持 mp3、flac、wav、ogg 等格式）
- 歌词面板，支持拖拽调整宽度，自动滚动高亮当前行
- 进度条拖拽 seek，音量调节
- 音频缓存，已播放过的歌曲再次播放无需重新下载
- 配置文件持久化保存至 `~/.config/GeMusic/config.yaml`

---

## 安装

### AUR（Arch Linux）

> *AUR 包正在准备中，敬请期待。*

### 手动编译

**依赖**

| 依赖 | 说明 |
|---|---|
| `cmake >= 3.24` | 构建系统 |
| `g++`（支持 C++23）| 编译器 |
| `openssl` | 网易云接口加密 |
| `curl` | HTTP 请求 |

其余依赖（FTXUI、nlohmann/json、yaml-cpp、spdlog 等）由 CMake 在构建时自动下载。

**步骤**

```bash
git clone https://github.com/Goldppx/GeMusic.git
cd GeMusic
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target GeMusic -j$(nproc)
sudo install -Dm755 build/GeMusic /usr/local/bin/GeMusic
```

---

## 快捷键

### 全局（任意页面有效）

| 按键 | 功能 |
|---|---|
| `q` | 退出 |
| `Space` | 播放 / 暂停 |
| `[` / `]` | 上一首 / 下一首 |
| `,` / `.` | 后退 10 秒 / 前进 10 秒 |
| `=` / `-` | 音量 +5 / -5 |
| `l` | 显示 / 隐藏歌词面板 |
| `↑` / `↓` | 列表导航 |

### 播放列表页

| 按键 | 功能 |
|---|---|
| `Enter` | 播放选中曲目 |
| `d` | 从队列中移除选中曲目 |

### 我的歌单页

| 按键 | 功能 |
|---|---|
| `Enter` | 打开歌单 / 替换队列并播放 |
| `a` | 将选中曲目追加到队列 |
| `r` | 刷新 |
| `Esc` | 返回歌单列表 |

### 本地文件页

| 按键 | 功能 |
|---|---|
| `Enter` | 替换队列并播放 |
| `a` | 追加到队列 |
| `r` | 重新扫描音乐库 |

### 设置页

| 按键 | 功能 |
|---|---|
| `e` | 用系统默认编辑器打开配置文件 |

---

## 贡献

欢迎提交 Issue 和 Pull Request。

- 代码风格遵循 Google C++ Style Guide，注释使用中文
- 提交信息遵循 Conventional Commits 规范
- 构建与测试：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

---

## 致谢

- [go-musicfox](https://github.com/anhoder/go-musicfox) — 同样优秀的终端网易云播放器，提供了很多思路上的参考
- [NeteaseCloudMusicApi](https://github.com/Binaryify/NeteaseCloudMusicApi) — 提供了完整的网易云 API 逆向文档，是本项目网络层的基础

---

## 许可证

[MIT](LICENSE)
