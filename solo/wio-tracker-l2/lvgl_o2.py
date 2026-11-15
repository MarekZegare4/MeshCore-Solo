# LVGL's object, style, layout and text code compiled for speed: -O2 instead
# of the framework's -Os. Opening a screen is mostly this code (style reads,
# flex, text measuring) and it is CPU-bound, not memory-bound -- small LVGL
# allocations in internal RAM changed nothing, -O2 here made screens open
# 10-15% faster (layout 12-20%). The drawing code is left at -Os: -O2 there
# gave 3-5% for another ~77 KB of flash.
Import("env")

def o2(env, node):
    flags = [f for f in env["CCFLAGS"] if f not in ("-Os", "-O1", "-O2", "-O3", "-Og")]
    return env.Object(node, CCFLAGS=flags + ["-O2"])

for pattern in ("*/lvgl/src/core/*", "*/lvgl/src/misc/*", "*/lvgl/src/layouts/*",
                "*/lvgl/src/widgets/label/*", "*/lvgl/src/font/*"):
    env.AddBuildMiddleware(o2, pattern)
