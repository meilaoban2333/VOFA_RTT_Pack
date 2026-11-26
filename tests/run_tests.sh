#!/bin/sh
# 主机端回归测试，在仓库根目录执行：sh tests/run_tests.sh
# 需要主机 gcc（用到 GNU ld 的 -Wl,--wrap），Windows 上用 MinGW gcc + Git Bash。
# 每种配置把 assets/ 下的源码复制到临时目录，用 sed 改宏后编译，仓库文件本身不动。
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
CC=${CC:-gcc}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
rc=0

# 32 位 x86 的 gcc 默认用 x87 的 80 位扩展精度算 float，舍入结果与 Cortex-M 的
# 单精度不一致；改用 SSE，让主机上的 float 运算与目标板一致
FPFLAGS=
if echo | $CC -dM -E - | grep -q '__i386__'; then
    FPFLAGS="-msse2 -mfpmath=sse"
fi

# 复制一份源码到 $WORK/<名字>，再对 vofa_rtt.h 逐条执行 sed 表达式
prep() {
    dir="$WORK/$1"
    shift
    mkdir -p "$dir"
    cp "$ROOT"/assets/RTT/* "$ROOT"/assets/examples/* "$dir"/
    for expr in "$@"; do
        sed -i "$expr" "$dir/vofa_rtt.h"
    done
}

# 编译并运行 test_vofa.c。本包文件（含测试本身）出现警告也算失败，
# SEGGER 原版文件的警告不归这里管
run_cfg() {
    name=$1
    shift
    prep "$name" "$@"
    dir="$WORK/$name"
    if $CC -std=gnu99 -O2 -Wall -Wextra $FPFLAGS -I"$dir" -I"$HERE/stub" -Wl,--wrap=SEGGER_RTT_Write \
        "$HERE/test_vofa.c" "$dir/SEGGER_RTT.c" "$dir/SEGGER_RTT_printf.c" \
        "$dir/vofa_rtt.c" "$dir/log_demo.c" -o "$dir/t" 2> "$dir/build.log"; then
        if grep -E '(vofa_rtt|log_demo|test_vofa)\.[ch]:[0-9]+:[0-9]+: warning' "$dir/build.log"; then
            echo "[$name] WARNINGS"
            rc=1
        fi
        "$dir/t" || rc=1
    else
        echo "[$name] BUILD FAILED"
        cat "$dir/build.log"
        rc=1
    fi
}

# 只编译不链接；第二个参数是期望结果 ok / fail
compile_expect() {
    label=$1
    want=$2
    shift 2
    if $CC "$@" > "$WORK/cc.log" 2>&1; then got=ok; else got=fail; fi
    if [ "$got" = "$want" ]; then
        echo "  PASS $label"
    else
        echo "  FAIL $label (expected $want, got $got)"
        cat "$WORK/cc.log"
        rc=1
    fi
}

JF='s/^#define VOFA_PROTOCOL .*/#define VOFA_PROTOCOL           VOFA_PROTO_JUSTFLOAT/'

run_cfg fw3
run_cfg fw0 's/^#define VOFA_DECIMALS .*/#define VOFA_DECIMALS           0/'
run_cfg jf "$JF"

echo "[compile checks]"
# Keil AC5 默认 C90，"语句之后再声明"会报 #268。gnu90 与 AC5 一样允许 // 注释和
# 变参宏，只把声明位置当错误，模拟 AC5 的这项检查
prep c90fw
prep c90jf "$JF"
for d in c90fw c90jf; do
    for f in vofa_rtt.c log_demo.c; do
        compile_expect "C90 $d/$f" ok -std=gnu90 -Wall -Werror=declaration-after-statement \
            -I"$WORK/$d" -I"$HERE/stub" -c "$WORK/$d/$f" -o "$WORK/$d/$f.o"
    done
done

# VOFA_SEND 通道数超过 VOFA_CH_MAX 必须编译报错。先把上限改成 2：
# 2 个参数要能编过（排除因别的原因编不过），3 个参数要报错
prep neg 's/^#define VOFA_CH_MAX .*/#define VOFA_CH_MAX             2/'
printf '#include "vofa_rtt.h"\nunsigned f(void) { return VOFA_SEND(1, 2); }\n' > "$WORK/neg/ok.c"
printf '#include "vofa_rtt.h"\nunsigned f(void) { return VOFA_SEND(1, 2, 3); }\n' > "$WORK/neg/bad.c"
compile_expect "VOFA_SEND at VOFA_CH_MAX compiles" ok -std=gnu99 -Werror=implicit-function-declaration -I"$WORK/neg" -c "$WORK/neg/ok.c" -o "$WORK/neg/ok.o"
compile_expect "VOFA_SEND over VOFA_CH_MAX fails" fail -std=gnu99 -Werror=implicit-function-declaration -I"$WORK/neg" -c "$WORK/neg/bad.c" -o "$WORK/neg/bad.o"

# LOG_xxx 表项宏：通道数为 0、超 VOFA_CH_MAX、超出变量大小（含小变量配大类型宏）必须编译报错。
# 用 gnu90 编，顺带确认宏在 Keil AC5 的 C90 下可用；ok.c 排除因别的原因编不过
prep lneg
lneg_cc() {
    printf '#include "log_demo.h"\nfloat a3[3]; float a9[9]; short s1;\nconst LogItem t[] = { %s };\n' "$3" > "$WORK/lneg/$2.c"
    compile_expect "$1" "$4" -std=gnu90 -Wall -Werror=declaration-after-statement \
        -I"$WORK/lneg" -I"$HERE/stub" -c "$WORK/lneg/$2.c" -o "$WORK/lneg/$2.o"
}
lneg_cc "LOG_xxx macros compile (C90)"   ok    'LOG_F32(a3, 3), LOG_I16(s1, 1)' ok
lneg_cc "LOG_F32 n over var size fails"  over  'LOG_F32(a3, 4)' fail
lneg_cc "LOG_F32 on 2-byte var fails"    small 'LOG_F32(s1, 1)' fail
lneg_cc "LOG_F32 n == 0 fails"           zero  'LOG_F32(a3, 0)' fail
lneg_cc "LOG_F32 n > VOFA_CH_MAX fails"  chmax 'LOG_F32(a9, 9)' fail

if [ $rc -eq 0 ]; then echo "ALL PASSED"; else echo "SOME TESTS FAILED"; fi
exit $rc
