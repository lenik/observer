# observer

`observer` 是一个 Linux 桌面自我状态观察器，技术栈为 C++17、
wxWidgets、SQLite 和 Meson。当前可执行程序是 `oremind`。

`oremind` 会在后台常驻，启动后立即弹出一次观察窗口，之后按设定间隔继续提醒。
每次提醒只做很轻的记录：一句当前活动，加上可选的能量、心情、接地评分。

## 功能

- 启动后隐藏主窗口，在后台运行。
- 显示托盘图标；左键点击立即弹出提示，右键菜单可唤醒、打开统计/历史或退出。
- 启动时会检测已有实例；如果已经运行，则唤醒已有实例、在控制台输出 warning，并退出当前进程。
- 启动后立即弹窗，之后通过 `wxTimer` 周期提醒。
- 深色提示界面，包含格言 canvas、emoji 评分控件、快捷键和滑入淡入动画。
- 可在弹窗右下角调整下一次间隔，点击单位可在分钟和秒之间切换。
- 弹窗右下角提供可点击的 `Quit` 标签，用于直接退出。
- 在 X11 下注册 `Win+Alt+G`，等待期间可立即弹出提示。
- 提交前会 trim 活动文本；空内容也会作为空日志记录。
- `energy`、`mood`、`grounding` 的默认值为 3，保持默认时视为未记录。
- 默认写入 SQLite，也可以把 `--sqlite-db` 指向目录来写每日文本日志。
- 连续 3 次按 Escape 取消后退出应用。

## 使用

```bash
oremind [OPTION]...
```

常用例子：

```bash
oremind
oremind --locale zh_CN
oremind --theme light
oremind --opacity 85
oremind --cancel 5
oremind --interval 0.5
oremind --interval 0
oremind --sqlite-db ~/.observer/observer.sqlite3
oremind --sqlite-db ~/.observer/logs/
```

选项：

- `-v`, `--verbose`：增加日志详细程度。
- `-q`, `--quiet`：减少日志输出。
- `-h`, `--help`：显示帮助。
- `-l`, `--locale LOCALE`：在加载翻译前设置界面语言，例如 `en`、`zh_CN`、`zh_TW`、`ja`、`ko`、`fr` 或 `de`。
- `-t`, `--theme light|dark`：选择浅色或深色主题。
- `-o`, `--opacity NUM`：设置弹窗最终不透明度，范围 `0` 到 `100`，默认 `75`。
- `-c`, `--cancel NUM`：连续取消 `NUM` 次后退出，默认 `3`。
- `-i`, `--interval NUM`：设置正常提醒间隔，单位为分钟，支持小数。`0` 表示启动弹窗后提交或取消即退出。
- `-w`, `--weekstart MmSs`：设置日历周起始日。`M`/`m` 表示周一，`S`/`s` 表示周日，默认周一。
- `-d`, `--sqlite-db PATH`：如果 `PATH` 是文件路径，则作为 SQLite 数据库；如果是已有目录或以 `/` 结尾，则作为日志目录。
- `--version`：显示版本和许可证信息。

## 快捷键

- `Enter`：提交。活动内容为空时记录为空日志。
- `Escape`：取消且不记录。默认连续取消 3 次会退出应用，可用 `--cancel` 修改。
- `Ctrl+S`：稍后 30 秒。
- `Ctrl+Q`：在弹窗中退出应用。
- `Ctrl+H`：打开统计视图。
- `Win+Alt+G`：等待期间立即弹出提示。该快捷键在 Linux/X11 下注册。
- `F1` / `F2`：能量减 / 加半格。
- `F3` / `F4`：心情减 / 加半格。
- `F5` / `F6`：接地减 / 加半格。

## 托盘和单实例

`oremind` 运行时会保留一个托盘图标：

- 左键点击托盘图标，立即显示提示弹窗。
- 右键点击托盘图标，菜单包含 `唤醒`、`统计 / 历史` 和 `退出`。
- `唤醒` 等同于 `Win+Alt+G`。

再次启动 `oremind` 时，不会创建第二个后台进程。新进程会通知已有实例弹出提示，
在标准错误输出以下 warning，然后退出：

```text
warning: oremind is already running; waking existing instance.
```

## 统计视图

在弹窗中按 `Ctrl+H`，或通过托盘图标右键菜单打开统计 / 历史。

统计窗口顶部工具栏提供日历、日、周、月、年、今天、上一页、下一页和关闭操作。
日历视图是自绘月历，包含农历日子、常见节假日高亮、今天和选中日标记，以及表示
记录的圆点和非空记录数。选中某一天后，日历右侧会显示该日记录数、空记录数、
总弹窗时长，以及平均能量、心情、接地值。

日、周、月、年视图会按当前粒度统计记录数、空记录数、总弹窗时长、平均能量、
平均心情和平均接地。组合图表用柱形显示记录数和弹窗时长，并在柱形之上叠加
能量、心情、接地折线。图表下方是可排序的通用历史记录表，字段包含弹窗时间、
提交时间、时长、能量、心情、接地、平均分、活动和格言。

统计窗口快捷键：

- `F1`：日历视图。
- `F5` / `F6` / `F7` / `F8`：日 / 周 / 月 / 年统计。
- `Left` / `Right`：日历视图中选择前一天 / 后一天；日/周/月/年视图中循环切换视图。
- `Up` / `Down`：日历视图中选择上一周 / 下一周；日/周/月/年视图中前后翻阅周期。
- `PageUp` / `PageDown`：前后翻阅周期。
- `Ctrl+T` 或 `Home`：回到今天。
- `Esc`：关闭统计窗口。

## 存储

默认 SQLite 数据库：

```text
~/.observer/observer.sqlite3
```

应用会自动创建父目录和表。默认评分会保存为 `NULL`。

当 `--sqlite-db` 指向目录时，`oremind` 会写每日日志：

```text
<datadir>/<yyyy-mm-dd>.log
```

记录格式。行首时间是弹窗时间，提交时间写在 `duration` 字段之后。

```text
hh:mm:ss e2.5 m3.5 g4.0 duration 1:23 submitted hh:mm:ss
    当前活动
```

评分保持默认值 3 时，该字段会被省略。

## 构建

### 依赖

Debian 系系统需要常规构建工具，以及 wxWidgets、SQLite、gettext 和本项目使用的
`bas-c` / `bas-cpp` 开发包：

```bash
sudo apt install meson ninja-build pkg-config gettext sqlite3 libsqlite3-dev \
    libwxgtk3.2-dev libx11-dev check
```

同时确保 `bas-c` 和 `bas-cpp` 的开发包可以被 `pkg-config` 找到。

### 配置和编译

```bash
meson setup _build
meson compile -C _build
```

从构建目录运行：

```bash
_build/oremind
```

### 安装

```bash
meson install -C _build
```

本地开发时可以使用符号链接辅助目标：

```bash
meson compile -C _build install-symlinks
meson compile -C _build uninstall-symlinks
```

## 翻译

翻译文件在 `po/` 目录。普通 Meson 构建会生成 `.mo` 文件。

快速检查语言：

```bash
LANGUAGE=zh_CN _build/oremind -h
LANGUAGE=ja _build/oremind -h
LANGUAGE=ko _build/oremind -h
```

## Debian 打包

```bash
dpkg-buildpackage -us -uc
```

## 许可证

Copyright (C) 2026 Lenik <observer@bodz.net>

本项目采用 **AGPL-3.0-or-later** 许可。完整文本和项目补充条款见 `LICENSE`。
