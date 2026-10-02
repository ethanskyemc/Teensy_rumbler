# -std=gnu++17 is a C++ flag. The SH8601 driver is C, so keep that
# standard on the C++ compiler only.

Import("env")


def move_cxx_standard():
    ccflags = list(env.get("CCFLAGS", []))
    kept = []
    moved = False
    index = 0
    while index < len(ccflags):
        flag = str(ccflags[index])
        nxt = str(ccflags[index + 1]) if index + 1 < len(ccflags) else ""
        if flag == "-std=gnu++17" or (flag == "-std" and nxt == "gnu++17"):
            moved = True
            index += 2 if flag == "-std" else 1
            continue
        kept.append(ccflags[index])
        index += 1
    if moved:
        env.Replace(CCFLAGS=kept)
        env.Append(CXXFLAGS=["-std=gnu++17"])


move_cxx_standard()
