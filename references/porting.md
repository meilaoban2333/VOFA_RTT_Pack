# 各构建方式的移植改动

三种构建方式配置 RTT 的具体改动。每种只需要做两件事：**3 个 .c 加入编译** + **RTT 目录加入 include 路径**。

以下均假设 RTT 源码放在工程的 `User/RTT/`，按实际路径调整。

---

## Keil MDK（uVision）

### 图形界面操作

1. **加源文件**：Project 窗格右键 Target → `Add Group...`，建组 `RTT`。
   右键 `RTT` 组 → `Add Existing Files to Group 'RTT'...`，选中三个 .c：
   - `User/RTT/SEGGER_RTT.c`
   - `User/RTT/SEGGER_RTT_printf.c`
   - `User/RTT/vofa_rtt.c`

2. **加头文件路径**：`Options for Target`（魔术棒）→ `C/C++` 标签 →
   `Include Paths` 右侧 `...` → 新增一行 `..\User\RTT`

3. **调试器按实际探针选**：`Options for Target` → `Debug` 标签 → 右侧下拉选
   `J-LINK / J-TRACE Cortex` 或 `CMSIS-DAP Debugger` 或 `ST-Link Debugger` → `Settings` 里 Port 选 `SW`。
   这里只影响 Keil 调试；RTT 采集由 `start_rtt.bat` 负责。CMSIS-DAP / ST-Link 下脚本占着探针，
   **Keil 调试前在脚本窗口按 `P` 释放**；下载改按 F7 编译即下载，见下面"边读边下载"。J-Link 无此限制。

### 直接改 .uvprojx（脚本化移植时用）

`<IncludePath>` 末尾追加 `;../User/RTT`：

```xml
<IncludePath>../Core/Inc;...原有路径...;../User/Inc;../User/RTT</IncludePath>
```

在 `<Groups>` 里加一个 Group（和其他 `<Group>` 平级）：

```xml
<Group>
  <GroupName>RTT</GroupName>
  <Files>
    <File>
      <FileName>SEGGER_RTT.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\RTT\SEGGER_RTT.c</FilePath>
    </File>
    <File>
      <FileName>SEGGER_RTT_printf.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\RTT\SEGGER_RTT_printf.c</FilePath>
    </File>
    <File>
      <FileName>vofa_rtt.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\User\RTT\vofa_rtt.c</FilePath>
    </File>
  </Files>
</Group>
```

`<FileType>1</FileType>` 表示 C 源文件，不要改。路径分隔符 uvprojx 里用反斜杠 `\`，
而 `<IncludePath>` 里用正斜杠 `/`，这是 Keil 自身的格式差异，照抄即可。

### 编译器版本

AC5(`__CC_ARM`) 和 AC6(ARMCLANG) 都支持，`SEGGER_RTT_Conf.h` 内部有对应分支自动选择。
AC5 已停止维护，新工程用 AC6。

### 边读边下载（CMSIS-DAP / ST-Link）

J-Link 跳过本节：SEGGER 软件允许 Keil 和脚本同时用一个 J-Link，照常 F8 下载即可。

CMSIS-DAP / ST-Link 同一时间只能有一个程序在用探针，脚本运行时 Keil 自带下载驱动打不开探针，
硬抢还会让两边命令串话。

**推荐：按 F7 就行（编译即下载，Keil 里什么都不用配）。** `start_rtt.bat` 放在工程根目录、`AUTOFLASH=1`
（默认）时，脚本发现新的 `.axf` 写完就自动下载，波形接着推；内容没变的重复编译会跳过。
`.axf` 在 `start_rtt.bat` 所在目录之外时，用 `FIRMWARE` 填它的路径。

**可选：让 F8 也走脚本。** 习惯按 F8 的话，把 F8 下载交给脚本：

1. `Options for Target` → `Utilities` 标签 → 选 `Use External Tool for Flash Programming`
2. `Command`：点 `...` 选工程里 `start_rtt.bat` 的完整路径
3. `Arguments`：`flash "#L"`（`#L` 是 Keil 的链接输出文件，即带完整路径的 `.axf`）
4. **不勾** `Run Independent`（Keil 要等下载结束、拿返回码判断成败）

之后 F8 的行为：脚本在运行 → 经脚本的 OpenOCD 下载、复位，波形接着推（实测断流约 0.5 秒）；
脚本没运行 → 临时起一个 OpenOCD 下载完退出。也可以在命令行直接用：
`start_rtt.bat flash "MDK-ARM\xxx\xxx.axf"`（`.axf` / `.elf` / `.hex` 都行，`.bin` 不带地址不支持）。

`Utilities` 这几项在 `.uvprojx` 里对应 `<Utilities>` 下的字段，具体映射未核实，
**脚本化移植时不要手写这几个 XML 字段**，在界面里配一次即可。

配了外部工具后 F7 和 F8 都能下载。脚本记录着最后一次经它烧进去的固件，同一份固件再编译出来会跳过；
如果 F8 会先自动编译，偶尔会重复烧录一次，不影响结果。

**不要**在脚本运行时保留 Keil 自带的 CMSIS-DAP 下载驱动去按 F8：Keil 不起单独的进程，
脚本只能在通信已经被打断后才让出探针。

---

## EIDE（VSCode 插件）

### 图形界面操作

1. **加源文件**：EIDE 项目资源管理器 → `Project Resources` 右键 → `New Virtual Folder`，
   建 `RTT` 文件夹 → 右键该文件夹 → `Add Existing Source Files`，选三个 .c。

2. **加头文件路径**：`Project Attributes` → `Include Path` → `+` → 选 `User/RTT` 目录。

3. **烧录器按实际探针选**：J-Link 用户在 `Flasher Configurations` → `Uploader` 选 `JLink` →
   展开设置，`Cpu Name` 填器件名（如 `STM32G070RB`），`Vendor` 填厂商（如 `ST`）。
   CMSIS-DAP / ST-Link 用户烧录器保持 `OpenOCD`（或 `pyOCD`）不用改，边看波形边下载有两种用法：
   - **点 Build 就行**（编译即下载）：`start_rtt.bat` 放在工程根目录，EIDE 输出到 `build/<目标名>/`
     的 `.elf` / `.axf` 写完后脚本自动下载。
   - **照常点下载按钮**：脚本发现 EIDE 起了 `openocd` / `pyocd` 进程就让出探针，下载完 2 秒后自动接回。
     对方刚启动的一瞬间偶尔报一次通信错误，重试即可。
   - 也可以把 `Uploader` 选 `Custom`，`Flash Command Line` 填 `.\start_rtt.bat flash "${ExecutableName}.hex"`
     （`${ExecutableName}` 是 EIDE 的输出文件路径不含扩展名；此配置未在 EIDE 里实测）。

   RTT 采集用 `start_rtt.bat`，它会自动找到 EIDE 自带的 OpenOCD。

### 直接改 .eide/eide.yml

在 `virtualFolder.folders` 下加一项（注意 YAML 缩进对齐其他同级 folder）：

```yaml
    - name: RTT
      files:
        - path: User/RTT/SEGGER_RTT.c
        - path: User/RTT/SEGGER_RTT_printf.c
        - path: User/RTT/vofa_rtt.c
      folders: []
```

在 `targets.<目标名>.custom_dep.incList` 里加一行：

```yaml
        - User/RTT
```

J-Link 用户把烧录器部分（`targets.<目标名>.uploadConfig`）改成 JLink（CMSIS-DAP / ST-Link 用户跳过）：

```yaml
        cpuInfo:
          cpuName: STM32G070RB    # 改成实际器件
          vendor: ST
    uploader: JLink               # 原来可能是 OpenOCD
```

注意 `uploader` 字段和 `uploadConfig` 是两处，都要改，只改一处不生效。

---

## CMake（含 STM32CubeIDE / arm-none-eabi-gcc）

### CubeMX 生成的 CMake 工程

CubeMX 生成的工程有 `cmake/stm32cubemx/CMakeLists.txt`，**不要直接改它**（重新生成会覆盖）。
改根目录 `CMakeLists.txt`，在 `add_executable` 之前或 `target_*` 区域加：

```cmake
# ---- SEGGER RTT + VOFA+ 波形输出 ----
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    User/RTT/SEGGER_RTT.c
    User/RTT/SEGGER_RTT_printf.c
    User/RTT/vofa_rtt.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    User/RTT
)
```

### 手写的通用 CMake 工程

如果用 `file(GLOB ...)` 收集源文件，确认 glob 覆盖到 `User/RTT/*.c`；
否则显式追加：

```cmake
list(APPEND SOURCES
    ${CMAKE_SOURCE_DIR}/User/RTT/SEGGER_RTT.c
    ${CMAKE_SOURCE_DIR}/User/RTT/SEGGER_RTT_printf.c
    ${CMAKE_SOURCE_DIR}/User/RTT/vofa_rtt.c
)
include_directories(${CMAKE_SOURCE_DIR}/User/RTT)
```

### Makefile（CubeMX 生成的 Makefile 工程）

`C_SOURCES` 追加三个 .c：

```makefile
C_SOURCES += \
User/RTT/SEGGER_RTT.c \
User/RTT/SEGGER_RTT_printf.c \
User/RTT/vofa_rtt.c
```

`C_INCLUDES` 追加：

```makefile
C_INCLUDES += -IUser/RTT
```

### GCC 优化等级注意

`-O2` / `-Os` 下 GCC 可能把 RTT 控制块 `_SEGGER_RTT` 优化掉或重排，导致调试器搜不到。
该结构体在 `SEGGER_RTT.c` 里已标 `SEGGER_RTT_CB_ALIGN` 且被外部引用，正常不会被裁掉。
若真的搜不到控制块，两个办法：
- 链接时确认 `_SEGGER_RTT` 符号存在：`arm-none-eabi-nm build/xxx.elf | grep SEGGER_RTT`
- 手动指定控制块地址，地址取上面 nm 的结果：
  - J-Link：Commander 中 `setRTTAddr <地址>`
  - CMSIS-DAP / ST-Link：`start_rtt.bat` 里 `RAM_START` 填该地址，`RAM_SIZE` 填 `0x100`

### CubeIDE 的图形操作

STM32CubeIDE 是 Eclipse 内核，不用 CMake 时：
1. 右键工程 → `Properties` → `C/C++ General` → `Paths and Symbols` → `Includes` →
   `Add...` → 填 `User/RTT`（勾 `Is a workspace path`）
2. 源文件放进工程目录后，右键工程 `Refresh`(F5) 即可自动纳入编译；
   若目录被排除，右键该目录 → `Resource Configurations` → `Exclude from Build` 取消勾选

---

## 查 `_SEGGER_RTT` 地址（CMSIS-DAP / ST-Link 搜索范围用）

CMSIS-DAP / ST-Link 走 OpenOCD，要知道控制块在哪段 RAM（J-Link 自己会找，不用管）。
默认搜整块 SRAM 通常就够，换芯片或搜不到时，按构建方式查精确地址：

| 构建方式 | 查法 |
|----------|------|
| Keil MDK | 在 Listing 输出目录找 `.map`（`Options for Target` → `Listing` → `Select Folder for Listings` 设定，CubeMX 工程一般是 `MDK-ARM/<工程名>/`），搜 `_SEGGER_RTT`，取 `Image Symbol Table` 里那行的地址 |
| GCC / CMake / Makefile | `arm-none-eabi-nm build/xxx.elf \| grep _SEGGER_RTT`，或在 `.map` 里搜 `_SEGGER_RTT` |
| EIDE | 编译输出目录 `build/<目标名>/` 下的 `.map`，搜 `_SEGGER_RTT` |

常见芯片的整块 SRAM 范围（默认搜索用）：

| 芯片 | `TARGETCFG`（OpenOCD） | `RAM_START` | `RAM_SIZE` |
|------|------------------------|-------------|------------|
| STM32G070RB | `target/stm32g0x.cfg` | `0x20000000` | `0x9000`（36KB） |
| STM32F103C8 | `target/stm32f1x.cfg` | `0x20000000` | `0x5000`（20KB） |
| STM32F407VE | `target/stm32f4x.cfg` | `0x20000000` | `0x20000`（128KB，不含 CCM） |

H7 等有多块 RAM 的芯片，控制块所在的块取决于链接脚本（DTCM `0x20000000` 或 AXI SRAM
`0x24000000`），**务必按 `.map` 查**，不要猜。

---

## 移植后自检清单

按顺序验证，任一步失败先解决再往下：

1. **编译过**：无 `undefined reference to SEGGER_RTT_Write` 之类错误。
   报这个错说明 `SEGGER_RTT.c` 没参与编译，回查步骤 1。
2. **头文件找得到**：无 `vofa_rtt.h: No such file`。报错说明 include 路径没配对。
3. **烧录后双击 `start_rtt.bat`**：看到 `probe: ...` 和 `RTT stream connected`。
   - J-Link 反复 `backend exited`：先查 `DEVICE` 器件名。
   - CMSIS-DAP / ST-Link 要看到 `RTT Control block found at 0x...`；反复 `No control block found`
     先查 `RAM_START`/`RAM_SIZE`（见上文"查 `_SEGGER_RTT` 地址"）。
4. **VOFA+ 连上 19021**：窗口标题里的 KB/s 不为 0，VOFA+ 能看到数据。速率一直为 0 时
   按 `R` 重连（会放行被 halt 的内核），或设 `RESET=1` 重开脚本。
5. **看到正弦+余弦两条波**：`VOFA_RTT_TestLoop()` 的自检波形，1Hz、幅值 100、相差 90°。
   看到这个说明整条链路通了，把 `TestLoop` 换成自己的变量即可。
6. **（CMSIS-DAP / ST-Link）编译即下载**：脚本开着，把 `vofa_rtt.h` 里 `VOFA_SINE_FREQ_HZ` 改成 `2.0f`
   后编译（Keil F7 / EIDE Build）。脚本窗口出现 `autoflash: ... changed, downloading ...` 和
   `flash: done, RTT continues`，VOFA+ 里正弦波变成 2Hz，说明下载链路也通了，改回 `1.0f` 即可。
