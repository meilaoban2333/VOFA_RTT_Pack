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

3. **调试器选 J-Link**：`Options for Target` → `Debug` 标签 →
   右侧下拉选 `J-LINK / J-TRACE Cortex` → `Settings` 里 Port 选 `SW`

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

---

## EIDE（VSCode 插件）

### 图形界面操作

1. **加源文件**：EIDE 项目资源管理器 → `Project Resources` 右键 → `New Virtual Folder`，
   建 `RTT` 文件夹 → 右键该文件夹 → `Add Existing Source Files`，选三个 .c。

2. **加头文件路径**：`Project Attributes` → `Include Path` → `+` → 选 `User/RTT` 目录。

3. **烧录器改 JLink**：`Flasher Configurations` → `Uploader` 选 `JLink` →
   展开设置，`Cpu Name` 填器件名（如 `STM32G070RB`），`Vendor` 填厂商（如 `ST`）。

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

烧录器部分（`targets.<目标名>.uploadConfig`）改成 JLink：

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

`-O2` / `-Os` 下 GCC 可能把 RTT 控制块 `_SEGGER_RTT` 优化掉或重排，导致 J-Link 搜不到。
该结构体在 `SEGGER_RTT.c` 里已标 `SEGGER_RTT_CB_ALIGN` 且被外部引用，正常不会被裁掉。
若真的搜不到控制块，两个办法：
- 链接时确认 `_SEGGER_RTT` 符号存在：`arm-none-eabi-nm build/xxx.elf | grep SEGGER_RTT`
- 在 J-Link 里手动指定控制块地址：Commander 中 `setRTTAddr <地址>`，地址取上面 nm 的结果

### CubeIDE 的图形操作

STM32CubeIDE 是 Eclipse 内核，不用 CMake 时：
1. 右键工程 → `Properties` → `C/C++ General` → `Paths and Symbols` → `Includes` →
   `Add...` → 填 `User/RTT`（勾 `Is a workspace path`）
2. 源文件放进工程目录后，右键工程 `Refresh`(F5) 即可自动纳入编译；
   若目录被排除，右键该目录 → `Resource Configurations` → `Exclude from Build` 取消勾选

---

## 移植后自检清单

按顺序验证，任一步失败先解决再往下：

1. **编译过**：无 `undefined reference to SEGGER_RTT_Write` 之类错误。
   报这个错说明 `SEGGER_RTT.c` 没参与编译，回查步骤 1。
2. **头文件找得到**：无 `vofa_rtt.h: No such file`。报错说明 include 路径没配对。
3. **烧录后跑 `start_rtt.bat`**：看到 `Connected` 且没有 `Cannot connect`。
   连不上先查 bat 里 `DEVICE` 器件名。
4. **VOFA+ 连上 19021**：能看到数据流进来。没数据先在 `J-Link>` 敲 `g`。
5. **看到正弦+余弦两条波**：`VOFA_RTT_TestLoop()` 的自检波形，1Hz、幅值 100、相差 90°。
   看到这个说明整条链路通了，把 `TestLoop` 换成自己的变量即可。
