struct SysVView { char* ptr; long len; };
extern "C" void* rh_arena_alloc(unsigned long long size);
extern "C" struct SysVView rh_bool_to_text(long long v) {
    char* buf = (char*)rh_arena_alloc(5);
    const char* s = v ? "benar" : "salah";
    for (int i = 0; i < 5; ++i) buf[i] = s[i];
    SysVView r = { buf, 5 };
    return r;
}
